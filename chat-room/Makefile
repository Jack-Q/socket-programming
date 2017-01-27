
CC=gcc

# since here use the BSD socket version, thus to be smoothly
# compiled in linux, the -D_DEFAULT_SOURCE toggle is required
CFLAGS=-std=c89 -Wall -g -D_DEFAULT_SOURCE -DDEBUG
TEST=./test


.PHONY: all test clean 
all: client server

client: client.c
	@${CC} ${CFLAGS} -o client client.c
server: server.c
	@${CC} ${CFLAGS} -o server server.c

test: all
	@./server &
	@./client localhost 7777
	@killall server

clean:
	@rm -f client server