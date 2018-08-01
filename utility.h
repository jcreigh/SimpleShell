#ifndef _UTILITY_H
#define _UTILITY_H

typedef enum {
	BUILTIN_MISSING,
	BUILTIN_OK,
	BUILTIN_EXIT,
	BUILTIN_ERROR,
	EXTERNAL_OK,
	EXTERNAL_ERROR,
	PIPE_OK,
	PIPE_ERROR
} status_t;

char* trimSpaces(char* str);
char* replaceHome(char* path);
char* getPwd();
char* buildPrompt();

#endif // _UTILITY_H
