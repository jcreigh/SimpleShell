/**
 * @file parser.h
 * @author Jessica Creighton
 * @date 2016-12-04
 */

#ifndef _PARSER_H
#define _PARSER_H

#include <stdlib.h>
enum parse_error_t {kParseOK, kUnexpectedEnd, kGivenNull, kRepeatedRedirect, kArgumentAfterRedirect, kNoArgs};
enum parse_token_t {kArgument, kRedirInput, kRedirOutput};

// Yay pseudo-OO :D

struct command_t {
	size_t argc;
	char** argv;
	size_t argc_max;
	char*  out_file;
	char*  in_file;
	struct command_t* pipe;
};

struct command_t* new_command();
enum parse_error_t parse(struct command_t* cmd, char* str);
void delete_command(struct command_t* cmd);
enum parse_error_t add_arg(struct command_t* cmd, char* arg, enum parse_token_t token_type);
int parser_tests();

void print_cmd(struct command_t* cmd);

#endif //_PARSER_H
