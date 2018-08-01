#include "utility.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>

char* trimSpaces(char* str) {
	while (*str == ' ') { str++; } // Trim leading spaces

	if (*str != '\0') { // We're empty, so no need to try to trim trailing spaces
		char* back = str + strlen(str) - 1;
		while (back > str && *back == ' ') { back--; } // Trim trailing spaces
		back[1] = '\0'; // Set new terminator
	}

	return str;
}

char* replaceHome(char* path) {
	char* fullPath = path;
	char* home = getenv("HOME");
	if (home == NULL) {
		return path;
	}
	int i = 0;
	while (home[i] != '\0' && home[i] == path[i]) {
		i++;
	}
	if (home[i] == '\0') {
		i--;
		fullPath[i] = '~';
		return fullPath + i;
	}
	return fullPath;
}

char* getPwd() {
	size_t s = sizeof(char*) * PATH_MAX;
	char* buf = (char*)malloc(s);
	while (getcwd(buf, s) == NULL) {
		free(buf);
		s *= 2;
		buf = (char*)malloc(s);
	};
	return buf;
}

char* buildPrompt() {
	char* tmp = getPwd();
	char* path = replaceHome(tmp);
	char* prompt = (char*)malloc(strlen(path) + 4);
	strcpy(prompt, path);
	strcat(prompt, " $ ");
	free(tmp);
	return prompt;
}
