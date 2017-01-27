CC=gcc
CFLAGS=-std=c99 -Wall -g -D_DEFAULT_SOURCE -DDEBUG

NAME=ping

.PHONY: all clean
all: $(NAME)
$(NAME): $(NAME).c
	$(CC) $(FLAGS) -o $@ $<
    
clean:
	rm -rf $(NAME)
