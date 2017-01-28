CC=gcc
CFLAGS=-std=c99 -Wall -g -D_DEFAULT_SOURCE -DDEBUG

NAME=traceroute

.PHONY: all clean
all: $(NAME)
$(NAME): $(NAME).c
	$(CC) $(FLAGS) -o $@ $<

clean:
	rm -rf $(NAME)
