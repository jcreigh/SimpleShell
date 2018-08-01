/**
 * @file builtins.c
 * @author Jessica Creighton
 * @date Created: 2016-11-14
 * @date Modified: 2016-12-04
 */

#include "utility.h"
#include "builtins.h"
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

struct builtin_t builtins[] = {
	{"set", builtin_set},
	{"delete", builtin_delete},
	{"print", builtin_print},
	{"cd", builtin_cd},
	{"pwd", builtin_pwd},
	{"help", builtin_help},
	{"exit", builtin_exit},
	{NULL, NULL}
};

int find_builtin(struct command_t* cmd) {
	for (int i = 0; builtins[i].name != NULL; i++) {
		if (strcmp(builtins[i].name, cmd->argv[0]) == 0) {
			return i; // It's a builtin, return its index
		}
	}
	return -1; // Not a builtin
}

status_t builtin_set(struct command_t* cmd) {
	if (cmd->argc > 1) {
		// Rebuild the string with spaces (that we're about to trim out)
		// in order to (mostly) replicate the behavior from Project 1 :/
		// Technically, this is causing us to strip out non-escaped quotes...
		// We could have preserved the behavior from Project 1 by making a copy
		// of the input line and use that if it's a builtin, but I wanted to do
		// a single pass parser and making a full copy ahead of time goes against
		// that.
		//
		// set from normal shells like bash and sh don't expect a space
		// delimiter, and doing it this way means we can't start/end with
		// a space in the variable or value. But, alas, Project 1.
		//
		// I probably could have gotten away with replicating normal shell behavior
		// since I doubt it'll be tested that harshly... Eh.
		char* s = cmd->argv[1];
		while (s < cmd->argv[cmd->argc - 1]) {
			if (*s == '\0') {
				*s = ' ';
			}
			s++;
		}

		char* args = cmd->argv[1];
		// Verify we have a = sign
		if (strchr(args, '=') != NULL) {
			// This strtok_r will never return NULL because we know we
			// have an =, so it's okay to use it straight away
			char* var = trimSpaces(strtok_r(NULL, "=", &args));
			// args will also never be NULL. It will minimally just
			// be a zero length string
			char* val = trimSpaces(args);

			if (setenv(var, val, 1) == 0) {
				printf("Setting %s = %s\n", var, val);
			} else {
				printf("Error setting variable: %s\n", strerror(errno));
			}
		} else {
			printf("Error: Usage: set varname = somevalue\n");
			return BUILTIN_ERROR;
		}
	} else {
		// Yeah, I hate this too.
		printf("Error: Usage: set varname = somevalue\n");
		return BUILTIN_ERROR;
	}
	return BUILTIN_OK;
}

status_t builtin_delete(struct command_t* cmd) {
	if (cmd->argc == 2) {
		unsetenv(cmd->argv[1]);
		printf("Deleting %s\n", cmd->argv[1]);
	} else {
		printf("Error: Usage: delete varname\n");
		return BUILTIN_ERROR;
	}
	return BUILTIN_OK;
}

status_t builtin_print(struct command_t* cmd) {
	if (cmd->argc == 2) {
		char* val = getenv(cmd->argv[1]);
		if (val == NULL) {
			printf("%s is unset\n", cmd->argv[1]);
		} else {
			printf("%s = %s\n", cmd->argv[1], val);
		}
	} else {
		printf("Error: Usage: print varname\n");
		return BUILTIN_ERROR;
	}
	return BUILTIN_OK;
}

status_t builtin_cd(struct command_t* cmd) {
	char* path;
	if (cmd->argc == 1) {
		// We want to change to our home directory
		// if we have no arguments
		if ((path = getenv("HOME")) == NULL) {
			path = ".";
		}
	} else if (cmd->argc == 2) {
		path = cmd->argv[1];
	} else {
		printf("cd: too many arguments\n");
		return BUILTIN_ERROR;
	}

	if (chdir(path) == -1) {
		printf("cd: %s\n", strerror(errno));
		return BUILTIN_ERROR;
	}

	return BUILTIN_OK;
}

status_t builtin_pwd(struct command_t* cmd) {
	// Correctly handle extremely long paths
	char* buf = getPwd();
	printf("%s\n", buf);
	free(buf);
	return BUILTIN_OK;
}

status_t builtin_help(struct command_t* cmd) {
	printf("set varname = somevalue\n");
	printf("delete varname\n");
	printf("print varname\n");
	printf("pwd\n");
	printf("cd [dir]\n");
	printf("exit\n");
	return BUILTIN_OK;
}

status_t builtin_exit(struct command_t* cmd) {
	return BUILTIN_EXIT;
}
