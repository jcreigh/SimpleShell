.PHONY: all clean

CC = gcc
FLAGS = -Wall -std=gnu11 -g

ifdef RUNTESTS
	FLAGS += -DRUNTESTS
endif

all: shell

shell: parser.c utility.c builtins.c main.c
	$(CC) $(FLAGS) $^ -lreadline -lcurses -o $@

clean:
	rm -f shell
