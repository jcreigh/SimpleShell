#ifndef _BUILTINS_H
#define _BUILTINS_H

#include "parser.h"
#include "utility.h"

typedef status_t (*builtin_func_t)(struct command_t* args);

struct builtin_t {
	const char* name;
	builtin_func_t func;
};

int find_builtin(struct command_t* cmd);

status_t builtin_set(struct command_t* cmd);
status_t builtin_delete(struct command_t* cmd);
status_t builtin_print(struct command_t* cmd);
status_t builtin_cd(struct command_t* cmd);
status_t builtin_pwd(struct command_t* cmd);
status_t builtin_help(struct command_t* cmd);
status_t builtin_exit(struct command_t* cmd);

#endif // _BUILTINS_H
