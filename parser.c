/**
 * @file parser.c
 * @author Jessica Creighton
 * @date 2016-12-05
 */

#include "parser.h"
#include "utility.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <stdio.h>

/**
 * Create a new command "object"
 * @return The new command object
 */
struct command_t* new_command() {
	struct command_t* cmd = (struct command_t*)malloc(sizeof(struct command_t));
	memset(cmd, 0, sizeof(struct command_t));
	cmd->argc_max = 256; // Initial max size
	cmd->argv = (char**)malloc(sizeof(char*) * cmd->argc_max);
	cmd->argv[0] = NULL; // Only need to set the first one
	return cmd;
}

/**
 * Parse a string and store the results into the provided command object
 * @param cmd Command object
 * @param str String to parse
 * @return Error code on error, else 0
 */
enum parse_error_t parse(struct command_t* cmd, char* str) {
	// Here be dragons :3

	if (!str) {
		return kGivenNull;
	}

	char* read_pos = str;
	char* write_pos = str; // Used for escape sequences

	char* arg = NULL;

	enum parse_token_t token_type = kArgument;

	struct command_t* working_cmd = cmd;

	while(*read_pos) {

		if (arg && *arg != '\0' && (*read_pos == ' ' || *read_pos == '<' || *read_pos == '>' || *read_pos == '|')) {
			// We're at a delimiter and we were just parsing an argument
			// so add it to the command
			int ret = add_arg(working_cmd, arg, token_type);
			if (ret != kParseOK) {
				return ret;
			}
			arg = NULL;
		}

		// Eat all the spaces up to the next argument
		while (*read_pos == ' ') { read_pos++; }

		if (*read_pos == '>' || *read_pos == '<') { // We're starting a redirect
			token_type = *read_pos == '>' ? kRedirOutput : kRedirInput;
			while (*(++read_pos) == ' ') {} // Eat spaces
			if (!*read_pos) {
				// End of input while expecting the redirect to go somewhere
				return kUnexpectedEnd;
			}
		} else if (*read_pos == '|') {
			// At a pipe
			while (*(++read_pos) == ' ') {} // Eat spaces
			if (!*read_pos) {
				// End of input while expecting the pipe to go somewhere
				return kUnexpectedEnd;
			}
			working_cmd->pipe = new_command();
			working_cmd = working_cmd->pipe;
			token_type = kArgument;
			continue;
		} else if (*read_pos && !arg && token_type != kArgument) {
			// We're starting a normal section after we've had a redirect
			return kArgumentAfterRedirect;
		}

		if (!arg) { // We're starting a new section
			if (write_pos != str) {
				*write_pos = '\0';
				write_pos++;
			}
			arg = write_pos;
		}

		if (*read_pos == '"' || *read_pos == '\'') {
			// We're arging a quoted section
			char quote = *read_pos;
			read_pos++;
			while (*read_pos) {
				if (*read_pos == '\\') { // Escape sequence
					read_pos++;
					if (!*read_pos) {
						// Reached end of input while in an escape sequence
						return kUnexpectedEnd;
					}
					// Check if we're trying to escape a backslash or quote
					if (!(*read_pos == '\\' || *read_pos == quote)) {
						// If we're in invalid escape, then we just write the backslash out too
						*write_pos = '\\';
						write_pos++;
					} // Else we skip over the backslash
				} else if (*read_pos == quote) {
					// We're at the end of the quoted section
					quote = '\0';
					read_pos++;
					break;
				}
				*write_pos = *read_pos;
				read_pos++;
				write_pos++;
			} // Can you tell I don't feel challenged?
			if (quote) {
				// We reached the end of our input but we were still in a quoted section
				return kUnexpectedEnd;
			}
		} else if (*read_pos) { // Start of normal section
			while (*read_pos && *read_pos != ' ' && *read_pos != '"' && *read_pos != '\'' && *read_pos != '<' && *read_pos != '>' && *read_pos != '|') {
				if (*read_pos == '\\') { // Escape sequence in nonquoted section
					read_pos++;
					if (!*read_pos) {
						// Reached end of input while in an escape sequence
						return kUnexpectedEnd;
					}
					if (!(*read_pos == '\\' || *read_pos == ' ' || *read_pos == '"' || *read_pos == '\'' || *read_pos == '|')) {
						// If we're in invalid escape, then we just write the backslash out too
						// This is technically different than bash, which for some reason just
						// drops it unless in a quoted
						*write_pos = '\\';
						write_pos++;
					} // Else we skip over the backslash
				}
				*write_pos = *read_pos;
				read_pos++;
				write_pos++;
			}
		}

	}
	*write_pos = '\0';

	if (arg && *arg != '\0') {
		// This pretty much means we didn't end on a space, so we need to add the argument
		int ret = add_arg(working_cmd, arg, token_type);
		if (ret != kParseOK) {
			return ret;
		}
	}

	return kParseOK;
}

/**
 * Free the memory used by the command object
 * @param cmd Command object
 */
void delete_command(struct command_t* cmd) {
	if (cmd) {
		if (cmd->pipe) {
			delete_command(cmd->pipe);
		}
		free(cmd->argv);
		free(cmd);
	}
}

/**
 * Store the argument in the command object, resizing the array if necessary
 * @param cmd Command object
 * @param arg Argument to store
 * @param state Type of token being added
 * @return An error if any occured
 */
enum parse_error_t add_arg(struct command_t* cmd, char* arg, enum parse_token_t token_type) {
	if (token_type == kArgument) {
		cmd->argv[cmd->argc++] = arg;

		if (cmd->argc == cmd->argc_max) {
			cmd->argc_max *= 2;
			char** tmp = (char**)malloc(sizeof(char*) * cmd->argc_max);
			// Copy current list over
			for (int i = 0; i < cmd->argc; i++) {
				tmp[i] = cmd->argv[i];
			}
			free(cmd->argv);
			cmd->argv = tmp;
		}

		cmd->argv[cmd->argc] = NULL;
	} else {
		if (cmd->argc == 0) {
			// We need at least one argument (the command) before we try to redirect
			return kNoArgs;
		}
		if (token_type == kRedirInput) {
			// Token is an input redirection
			if (cmd->in_file) {
				return kRepeatedRedirect;
			}
			cmd->in_file = arg;
		} else if (token_type == kRedirOutput) {
			// Token is an output redirection
			if (cmd->out_file) {
				return kRepeatedRedirect;
			}
			cmd->out_file = arg;
		}
	}
	return kParseOK;
}

void print_indent(int indent) {
	for (int i = 0; i < indent; i++) {
		printf(" ");
	}
}

void print_cmd_indent(struct command_t* cmd, int indent) {
	print_indent(indent); printf("Command: %s\n", cmd->argv[0]);
	print_indent(indent); printf("Arguments: %ld\n", cmd->argc);
	for (int i = 0; i < cmd->argc; i++) {
		print_indent(indent); printf(" %s\n", cmd->argv[i]);
	}
	print_indent(indent); printf("Redirects:\n In : %s\n Out: %s\n", cmd->in_file, cmd->out_file);
}

void print_cmd(struct command_t* cmd) {
	int i = 0;
	while (cmd) {
		print_cmd_indent(cmd, i);
		if (cmd->pipe) {
			print_indent(i); printf("Pipe:\n");
		}
		i++;
		cmd = cmd->pipe;
	}

}

void dump_ascii(char* str, size_t len) {
	for (int i = 0; i < len; i++) {
		printf("%c", str[i] < 32 || str[i] >= 127 ? '.' : str[i]);
	}
	printf("\n");
}

/**
 * Run parser tests
 */
int parser_tests() {
	char buf[768];
	memset(buf, 0, 768);
	struct command_t* cmd = new_command();

	// Check being given a zero length string
	assert(parse(cmd, buf) == kParseOK);
	assert(cmd->argc == 0);

	// Check combination quotes and regular
	strcpy(buf, " foo \"foo \"=\"  bar\" ");
	/*dump_ascii(buf, 32);*/
	assert(parse(cmd, buf) == kParseOK);
	/*printf("%d\n", parse(cmd, buf));*/
	/*exit(1);*/

	assert(cmd->argc == 2);
	assert(strcmp(cmd->argv[0], "foo") == 0);
	assert(strcmp(cmd->argv[1], "foo =  bar") == 0);
	assert(memcmp(buf, "foo\0foo =  bar\0", 15) == 0);

	// Check ending while in a quoted section
	strcpy(buf, " foo \"  bar ");
	cmd->argc = 0;
	assert(parse(cmd, buf) == kUnexpectedEnd);

	// Check ending with an escape
	strcpy(buf, " foo barr \\");
	cmd->argc = 0;
	assert(parse(cmd, buf) == kUnexpectedEnd);

	// Check being given a NULL
	assert(parse(cmd, NULL) == kGivenNull);

	// Check simple quoting
	strcpy(buf, "foo bar  \"baz qux\"  quux");
	cmd->argc = 0;
	assert(parse(cmd, buf) == kParseOK);
	assert(cmd->argc == 4);
	assert(strcmp(cmd->argv[0], "foo") == 0);
	assert(strcmp(cmd->argv[1], "bar") == 0);
	assert(strcmp(cmd->argv[2], "baz qux") == 0);
	assert(strcmp(cmd->argv[3], "quux") == 0);
	assert(memcmp(buf, "foo\0bar\0baz qux\0quux\0", 21) == 0);

	// Check double quotes
	strcpy(buf, "foo \"b\\ar\" \"\\\\\" \"b\\\\ar\" \"b\\\"ar\"");
	cmd->argc = 0;
	assert(parse(cmd, buf) == kParseOK);
	assert(cmd->argc == 5);
	assert(strcmp(cmd->argv[0], "foo") == 0);
	assert(strcmp(cmd->argv[1], "b\\ar") == 0);
	assert(strcmp(cmd->argv[2], "\\") == 0);
	assert(strcmp(cmd->argv[3], "b\\ar") == 0);
	assert(strcmp(cmd->argv[4], "b\"ar") == 0);
	assert(memcmp(buf, "foo\0b\\ar\0\\\0b\\ar\0b\"ar\0", 21) == 0);

	// Check single quotes
	strcpy(buf, "foo \'b\\ar\' \'\\\\\' \'b\\\\ar\' \'b\\\'ar\'");
	cmd->argc = 0;
	assert(parse(cmd, buf) == kParseOK);
	assert(cmd->argc == 5);
	assert(strcmp(cmd->argv[0], "foo") == 0);
	assert(strcmp(cmd->argv[1], "b\\ar") == 0);
	assert(strcmp(cmd->argv[2], "\\") == 0);
	assert(strcmp(cmd->argv[3], "b\\ar") == 0);
	assert(strcmp(cmd->argv[4], "b\'ar") == 0);
	assert(memcmp(buf, "foo\0b\\ar\0\\\0b\\ar\0b\'ar\0", 21) == 0);

	// Check non-quoted escaped
	strcpy(buf, "foo\\ bar \\\"baz\\' \\\\qux");
	cmd->argc = 0;
	assert(parse(cmd, buf) == kParseOK);
	assert(cmd->argc == 3);
	assert(strcmp(cmd->argv[0], "foo bar") == 0);
	assert(strcmp(cmd->argv[1], "\"baz'") == 0);
	assert(strcmp(cmd->argv[2], "\\qux") == 0);
	assert(memcmp(buf, "foo bar\0\"baz'\0\\qux\0", 19) == 0);

	// Check >256 arguments
	for (int i = 0; i < 347; i++) {
		buf[2 * i] = 'a';
		buf[2 * i + 1] = ' ';
	}
	cmd->argc = 0;
	assert(parse(cmd, buf) == kParseOK);
	assert(cmd->argc == 347);
	assert(cmd->argc_max == 512);

	// Check redirection

	// Basic redirection
	strcpy(buf, "foo >bar <baz");
	cmd->argc = 0;
	assert(parse(cmd, buf) == kParseOK);
	assert(cmd->argc == 1);
	assert(strcmp(cmd->argv[0], "foo") == 0);
	assert(cmd->out_file && strcmp(cmd->out_file, "bar") == 0);
	assert(cmd->in_file && strcmp(cmd->in_file, "baz") == 0);
	assert(memcmp(buf, "foo\0bar\0baz\0", 12) == 0);

	// Argument after redirect
	strcpy(buf, "foo >bar arg <baz");
	cmd->argc = 0;
	cmd->out_file = cmd->in_file = NULL;
	assert(parse(cmd, buf) == kArgumentAfterRedirect);

	// Repeated redirect
	strcpy(buf, "foo >bar <baz >qux");
	cmd->argc = 0;
	cmd->out_file = cmd->in_file = NULL;
	assert(parse(cmd, buf) == kRepeatedRedirect);

	// No spaces redirects
	strcpy(buf, "foo>bar<baz");
	cmd->argc = 0;
	cmd->out_file = cmd->in_file = NULL;
	assert(parse(cmd, buf) == kParseOK);
	assert(strcmp(cmd->argv[0], "foo") == 0);
	assert(cmd->out_file && strcmp(cmd->out_file, "bar") == 0);
	assert(cmd->in_file && strcmp(cmd->in_file, "baz") == 0);
	assert(memcmp(buf, "foo\0bar\0baz\0", 12) == 0);

	// Spaces redirect
	strcpy(buf, "foo  >  bar  <   baz  ");
	cmd->argc = 0;
	cmd->out_file = cmd->in_file = NULL;
	assert(parse(cmd, buf) == kParseOK);
	assert(strcmp(cmd->argv[0], "foo") == 0);
	assert(cmd->out_file && strcmp(cmd->out_file, "bar") == 0);
	assert(cmd->in_file && strcmp(cmd->in_file, "baz") == 0);
	assert(memcmp(buf, "foo\0bar\0baz\0", 12) == 0);

	// Redirects with quotes
	strcpy(buf, "foo >bar\"baz \" < \"bar \"baz" );
	cmd->argc = 0;
	cmd->out_file = cmd->in_file = NULL;
	assert(parse(cmd, buf) == kParseOK);
	assert(strcmp(cmd->argv[0], "foo") == 0);
	assert(cmd->out_file && strcmp(cmd->out_file, "barbaz ") == 0);
	assert(cmd->in_file && strcmp(cmd->in_file, "bar baz") == 0);
	assert(memcmp(buf, "foo\0barbaz \0bar baz\0", 20) == 0);

	// Redirects at end
	strcpy(buf, "foo >  ");
	cmd->argc = 0;
	cmd->out_file = cmd->in_file = NULL;
	assert(parse(cmd, buf) == kUnexpectedEnd);

	// Redirects at start
	strcpy(buf, ">foo");
	cmd->argc = 0;
	cmd->out_file = cmd->in_file = NULL;
	assert(parse(cmd, buf) == kNoArgs);

	// Basic pipe
	strcpy(buf, "foo | bar");
	cmd->argc = 0;
	cmd->out_file = cmd->in_file = NULL;
	assert(parse(cmd, buf) == kParseOK);
	assert(strcmp(cmd->argv[0], "foo") == 0);
	assert(cmd->pipe != NULL);
	assert(strcmp(cmd->pipe->argv[0], "bar") == 0);

	// No spaces pipe
	strcpy(buf, "foo|bar");
	cmd->argc = 0;
	delete_command(cmd->pipe); cmd->pipe = NULL;
	cmd->out_file = cmd->in_file = NULL;
	assert(parse(cmd, buf) == kParseOK);
	assert(strcmp(cmd->argv[0], "foo") == 0);
	assert(cmd->pipe != NULL);
	assert(strcmp(cmd->pipe->argv[0], "bar") == 0);

	// Pipe to nowhere
	strcpy(buf, "foo|");
	cmd->argc = 0;
	delete_command(cmd->pipe); cmd->pipe = NULL;
	cmd->out_file = cmd->in_file = NULL;
	assert(parse(cmd, buf) == kUnexpectedEnd);

	// Pipe to nowhere
	strcpy(buf, "foo|");
	cmd->argc = 0;
	delete_command(cmd->pipe); cmd->pipe = NULL;
	cmd->out_file = cmd->in_file = NULL;
	assert(parse(cmd, buf) == kUnexpectedEnd);

	// Pipe with redirects
	strcpy(buf, "foo < qux | bar > quux");
	cmd->argc = 0;
	delete_command(cmd->pipe); cmd->pipe = NULL;
	cmd->out_file = cmd->in_file = NULL;
	assert(parse(cmd, buf) == kParseOK);
	assert(strcmp(cmd->argv[0], "foo") == 0);
	assert(cmd->in_file && strcmp(cmd->in_file, "qux") == 0);
	assert(cmd->pipe != NULL);
	assert(strcmp(cmd->pipe->argv[0], "bar") == 0);
	assert(cmd->pipe->out_file && strcmp(cmd->pipe->out_file, "quux") == 0);

	// Multiple pipes
	strcpy(buf, "foo < qux | bar | baz > quux");
	cmd->argc = 0;
	delete_command(cmd->pipe); cmd->pipe = NULL;
	cmd->out_file = cmd->in_file = NULL;
	assert(parse(cmd, buf) == kParseOK);
	assert(strcmp(cmd->argv[0], "foo") == 0);
	assert(cmd->in_file && strcmp(cmd->in_file, "qux") == 0);
	assert(cmd->pipe != NULL);
	assert(strcmp(cmd->pipe->argv[0], "bar") == 0);
	assert(cmd->pipe->pipe != NULL);
	assert(strcmp(cmd->pipe->pipe->argv[0], "baz") == 0);
	assert(cmd->pipe->pipe->out_file && strcmp(cmd->pipe->pipe->out_file, "quux") == 0);

	// Cleanup
	delete_command(cmd);
	return 0;
}
