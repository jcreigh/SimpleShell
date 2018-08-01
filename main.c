/**
 * @file main.c
 */
#include "builtins.h"
#include "utility.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

extern struct builtin_t builtins[];

status_t execute_command(struct command_t* cmd);
status_t execute_command_child(struct command_t* cmd, int pipefd[], pid_t pgid);
status_t execute_builtin(struct command_t* cmd, int pipefd[]);
status_t execute_external(struct command_t* cmd);

pid_t pipeline_pgid;
sigset_t sigmask;

void handle_sigint(int sig) {
	// printf is not async-signal-safe (see man 7 signal)
	write(STDOUT_FILENO, "\n", 1);
	rl_on_new_line();
	rl_forced_update_display(); // Redisplay prompt.. probably safe?
}

int main(int argc, char** argv) {
#ifdef RUNTESTS
	parser_tests();
	return 0;
#endif

	// Use sigaction because on Paris the handler is uninstalled for some reason
	// after being triggered once
	struct sigaction action;
	memset(&action, 0, sizeof(sigaction));
	action.sa_handler = handle_sigint;
	if (sigaction(SIGINT, &action, NULL) < 0) {
		perror("Failed to setup signal handler");
	}

	char* s;
	char* prompt = buildPrompt();
	status_t ret;

	// Initialize shell by ignoring certain job control signals
	signal(SIGTSTP, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);

	pipeline_pgid = 0;

	while ((s = readline(prompt))) {

		add_history(s);

		struct command_t* cmd = new_command();
		enum parse_error_t pe = parse(cmd, s);

		if (pe == kUnexpectedEnd) {
			printf("Unexpected end of command\n");
		} else if (pe == kRepeatedRedirect) {
			printf("Redirection was repeated\n");
		} else if (pe == kArgumentAfterRedirect) {
			printf("Redirection must occur after arguments\n");
		} else if (pe == kNoArgs) {
			printf("A command must be specified\n");
		} else if (cmd->argc > 0) {
			// We're a command, execute it
			ret = execute_command(cmd);

			// EXIT is the only return we really care about, errors
			// have already been taken care of
			if (ret == BUILTIN_EXIT) {
				// Clean up
				delete_command(cmd);
				break;
			}
		}

		delete_command(cmd);
		free(s);
		free(prompt);
		prompt = buildPrompt();

	}
	// s will be NULL on ctrl-d with no buffer, but
	// that's okay because then free does nothing
	free(s);
	free(prompt);
	return 0;
}

status_t execute_command(struct command_t* cmd) {
	status_t ret;
	size_t child_count = 0;

	int fd[2] = {STDIN_FILENO, STDOUT_FILENO};
	int builtin_idx = find_builtin(cmd);

	// Block signals until we finish with the pipeline
	sigset_t mask;
	sigemptyset(&mask);
	/*sigaddset(&mask, SIGINT);  // We want to block INT*/
	sigaddset(&mask, SIGCHLD); // and CHLD
	sigprocmask(SIG_BLOCK, &mask, &sigmask);

	if (cmd->pipe) {
		ret = PIPE_OK;
		// We have a pipeline, need to set up all the pipage
		int pipefd[2];

		while (cmd) {
			if (child_count > 0) {
				// We have somewhere to pipe from
				if (fd[0] >= 0 && fd[0] != STDIN_FILENO) {
					if (close(fd[0]) < 0) {
						perror("Failed to close input pipe");
						ret = PIPE_ERROR;
						break;
					}
				}
				fd[0] = pipefd[0];
			} else {
				fd[0] = STDIN_FILENO;
			}

			if (cmd->pipe) {
				// We have somewhere to pipe to

				if (pipe(pipefd) < 0) {
					perror("Failed to create pipe");
					ret = PIPE_ERROR;
					break;
				}

				fd[1] = pipefd[1];

				if (child_count == 0) {
					// We want the child to close this, and not use it, so set it to negative
					// so set it to negative to signify this
					fd[0] = -pipefd[0];
				}
			} else {
				fd[1] = STDOUT_FILENO;
			}

			if (child_count > 0 || builtin_idx < 0) {
				// We're either not builtin, or not leftmost
				// so we want to execute it as a child
				pid_t pid = execute_command_child(cmd, fd, pipeline_pgid);
				if (pipeline_pgid == 0) {
					pipeline_pgid = pid;
				}
				if (setpgid(pid, pipeline_pgid) < 0) {
					perror("Failed to set process group");
				}
			} else {
				// We're a builtin and leftmost, execute right now
				execute_builtin(cmd, fd);
			}
			child_count++;

			if (fd[1] != STDOUT_FILENO) {
				if (close(pipefd[1]) < 0) {
					perror("Failed to close output pipe");
					ret = PIPE_ERROR;
				}
			}

			cmd = cmd->pipe;
		}

		if (child_count > 0) {
			if (close(pipefd[0]) < 0) {
				perror("Failed to close input pipe");
				ret = PIPE_ERROR;
			}
		}

	} else {
		// No pipeline, we can just run the command regularly
		if (builtin_idx < 0) {
			pipeline_pgid = execute_command_child(cmd, fd, 0);
			if (setpgid(pipeline_pgid, pipeline_pgid) < 0) {
				perror("Failed to set process group");
			}
			ret = EXTERNAL_OK;
		} else {
			ret = execute_builtin(cmd, fd);
		}
		child_count++;
	}

	if (child_count > 0 && builtin_idx >= 0) {
		child_count--; // One of our childen was fake, no need to wait for it
	}

	if (child_count > 0 && ret == PIPE_ERROR) {
		// Murder the children
		/*printf("Murdering the children %d (%ld of them)\n", pipeline_pgid, child_count);*/
		if (pipeline_pgid && killpg(pipeline_pgid, SIGINT) < 0) {
			perror("Failed to murder the children");
		}
	}

	// Give child group control of terminal
	if (pipeline_pgid) {
		tcsetpgrp(STDIN_FILENO, pipeline_pgid);
	}

	// Done creating pipeline, restore signal mask
	sigprocmask(SIG_SETMASK, &sigmask, NULL);


	int child_killed = 0;
	while (child_count--) { // Wait for each of the children
		int status = 0;
		pid_t pid = waitpid(-1, &status, 0);
		// The exec will replace the signal handler, so you can't capture it and make it print something
		// so use the exit status
		if (WIFSIGNALED(status)) {
			if (!child_killed) {
				// Personally, I don't want to print the \n first, other shells don't, but it makes sure
				// our message is on its own line and if I don't do it it'll seem like a mistake rather
				// than a design decision
				printf("\n");
				child_killed = 1;
			}
			printf("Child %d killed with signal %d (%s)\n", pid, WTERMSIG(status), strsignal(WTERMSIG(status)));
		}
		if (WEXITSTATUS(status) == 127) {
			/*printf("Child died horribly, kill everyone in pgid (%d)\n", pgid);*/
			killpg(pipeline_pgid, SIGINT);
			ret = EXTERNAL_ERROR;
		}
	}

	pipeline_pgid = 0;

	// Get control of terminal back
	tcsetpgrp(STDIN_FILENO, getpgid(0));
	return ret;
}

status_t execute_command_child(struct command_t* cmd, int pipefd[], pid_t pgid) {
	pid_t pid = 0;
	if ((pid = fork()) < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		perror("Error forking");
	} else if (pid == 0) {
		// Child
		// Delete signal handlers
		if (signal(SIGINT, SIG_DFL) == SIG_ERR ||
			signal(SIGTSTP, SIG_DFL) == SIG_ERR ||
			signal(SIGTTIN, SIG_DFL) == SIG_ERR ||
			signal(SIGTTOU, SIG_DFL) == SIG_ERR) {
			perror("Failed to delete signal handler");
		}

		// Unblock signals
		sigprocmask(SIG_SETMASK, &sigmask, NULL);

		if (pgid > 0) {
			// Set a process group
			if (setpgid(0, pgid) < 0) {
				perror("child: Failed to set process group");
			}
		}

		if (pipefd[0] != STDIN_FILENO) {
			if (pipefd[0] >= 0) {
				if (dup2(pipefd[0], STDIN_FILENO) < 0) {
					perror("Failed to redirect stdin");
				}
			} else {
				pipefd[0] = -pipefd[0];
			}
			if (close(pipefd[0]) < 0) {
				perror("Failed to close input pipe");
			}
		}

		if (pipefd[1] != STDOUT_FILENO) {
			if (pipefd[1] >= 0) {
				if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
					perror("Failed to redirect stdout");
				}
			} else {
				pipefd[1] = -pipefd[1];
			}
			if (close(pipefd[1]) < 0) {
				perror("Failed to close output pipe");
			}
		}

		int fd[2] = {STDIN_FILENO, STDOUT_FILENO};
		status_t ret = execute_builtin(cmd, fd);
		if (ret == BUILTIN_EXIT) {
			// Well, we're in a fork, so this will just
			// kill the fork.
			close(STDIN_FILENO);
			exit(127);
		} else if (ret == BUILTIN_MISSING) {
			execute_external(cmd);
			// If we're here, we failed to run the child and have already printed
			// out an error.
			exit(127);
		}
		exit(0);
	}
	return pid;
}

status_t execute_builtin(struct command_t* cmd, int pipefd[]) {
	status_t ret = BUILTIN_MISSING;
	int builtin_idx = find_builtin(cmd);

	if (builtin_idx < 0) {
		return BUILTIN_MISSING;
	}

	// Builtin found
	ret = BUILTIN_OK;

	int stdout_dup, stdin_dup, out_fd, in_fd;
	stdout_dup = stdin_dup = out_fd = in_fd = -1;

	// Setup redirects if they exist

	if (cmd->in_file || (pipefd[0] >= 0 && pipefd[1] != STDIN_FILENO)) {
		// We're redirecting stdin somewhere, so save the old one
		if ((stdin_dup = dup(STDIN_FILENO)) < 0) {
			perror("builtin: Failed to store stdin fd");
			ret = BUILTIN_ERROR;
		}
	}
	if (cmd->in_file) { // We're redirecting to a file
		if ((in_fd = open(cmd->in_file, O_RDONLY)) < 0) {
			perror("builtin: Failed to open input file");
			ret = BUILTIN_ERROR;
		}
	} else if (pipefd[0] != STDIN_FILENO) { // We're rediricting to a pipe
		in_fd = pipefd[0];
	}
	if (in_fd >= 0 && dup2(in_fd, STDIN_FILENO) < 0) { // Do the redirection
		perror("builtin: Failed to redirect stdin");
		ret = BUILTIN_ERROR;
	}

	if (ret == BUILTIN_OK) {
	if (cmd->out_file || (pipefd[1] >= 0 && pipefd[1] != STDOUT_FILENO)) {
		// We're redirecting stdout somewhere, so save the old one
		if ((stdout_dup = dup(STDOUT_FILENO)) < 0) {
			perror("builtin: Failed to store stdout fd");
			ret = BUILTIN_ERROR;
		}
	}
	if (cmd->out_file) { // We're redirecting to a file
		if ((out_fd = open(cmd->out_file, O_WRONLY|O_CREAT|O_TRUNC, 0666)) < 0) {
			perror("builtin: Failed to open output file");
			ret = BUILTIN_ERROR;
		}
	} else if (pipefd[1] != STDOUT_FILENO) { // We're rediricting to a pipe
		out_fd = pipefd[1];
	}
	if (out_fd >= 0 && dup2(out_fd, STDOUT_FILENO) < 0) { // Do the redirection
		perror("builtin: Failed to redirect stdout");
		ret = BUILTIN_ERROR;
	}
	}

	// Execute the builtin
	if (ret == BUILTIN_OK) {
		ret = (*(builtins[builtin_idx].func))(cmd);
	}

	// Reset stdout and stdin back to what they were
	if (cmd->out_file) {
		if (out_fd >= 0 && close(out_fd) < 0) {
			perror("builtin: Failed to close output file");
			ret = BUILTIN_ERROR;
		}
	}
	if (stdout_dup >= 0) {
		// Reset stdout
		if (dup2(stdout_dup, STDOUT_FILENO) < 0) {
			perror("builtin: Failed to reset stdout");
			ret = BUILTIN_ERROR;
		}
		if (close(stdout_dup) < 0) {
			// There's literally nothing we can do here... it's kind of pointless
			// to actually check if it's successful or not. If it's not, there's
			// something seriously wrong.
			perror("builtin: Failed to close stored stdout fd");
			ret = BUILTIN_ERROR;
		}
	}
	if (cmd->in_file) {
		if (in_fd >= 0 && close(in_fd) < 0) {
			perror("builtin: Failed to close input file");
			ret = BUILTIN_ERROR;
		}
	}
	if (stdin_dup >= 0) {
		// Reset stdin
		if (dup2(stdin_dup, STDIN_FILENO) < 0) {
			perror("builtin: Failed to reset stdin");
			ret = BUILTIN_ERROR;
		}
		if (close(stdin_dup) < 0) {
			perror("builtin: Failed to close stored stdin fd");
			ret = BUILTIN_ERROR;
		}
	}
	return ret;
}

status_t execute_external(struct command_t* cmd) {
	status_t ret = EXTERNAL_OK;
	int out_fd, in_fd;
	out_fd = in_fd = -1;
	if (cmd->out_file) {
		if ((out_fd = open(cmd->out_file, O_WRONLY|O_CREAT|O_TRUNC, 0666)) < 0) {
			perror("external: Failed to open output file");
			ret = EXTERNAL_ERROR;
		} else if (dup2(out_fd, STDOUT_FILENO) < 0) {
			perror("external: Failed to redirect stdout");
			ret = EXTERNAL_ERROR;
		}
	}
	if (cmd->in_file) {
		if ((in_fd = open(cmd->in_file, O_RDONLY)) < 0) {
			perror("external: Failed to open input file");
			ret = EXTERNAL_ERROR;
		} else if (dup2(in_fd, STDIN_FILENO) < 0) {
			perror("external: Failed to redirect stdin");
			ret = EXTERNAL_ERROR;
		}
	}
	if (ret == EXTERNAL_OK) {
		execvp(cmd->argv[0], cmd->argv);
		// We failed to execute!
		perror(cmd->argv[0]);
	}

	// Close our files, I guess. Kind of pointless
	// since we're about to exit and they'll be
	// force closed anyway
	if (out_fd >= 0 && close(out_fd) < 0) {
		perror("external: Failed to close output file");
	}
	if (in_fd >= 0 && close(in_fd) < 0) {
		perror("external: Failed to close input file");
	}
	exit(1);
}
