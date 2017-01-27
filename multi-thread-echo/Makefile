
CC=gcc
# since here use the BSD socket version, thus to be smoothly
# compiled in linux, the -D_BSD_SOURCE toggle is required
CFLAGS=-std=c89 -Wall -O2 -D_BSD_SOURCE
TEST=./test

all: cli svc

cli: client.c
	@${CC} ${CFLAGS} -o $@ $<
svc: server.c
	@${CC} ${CFLAGS} -o $@ $<


.PHONY: all test clean 
test: all
	@./svc &
	@./cli 127.0.0.1 2111 
	@killall svc

clean:
	@rm -f cli svc 