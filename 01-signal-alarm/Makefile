CC=gcc
CFLAGS=-std=c99 -Wall -g -D_DEFAULT_SOURCE -O3 -DDEBUG
TEST=./test
SHELL=/bin/bash

SEND=sender
RECV=receiver

.PHONY: all test clean
all: $(SEND) $(RECV)

%: %.c
	@$(CC) $(CFLAGS) -o $@ $<

test: all | test-dir
	@dd status=none if=/dev/urandom of=test/input_file bs=5M count=1
	@/usr/bin/time -f"Receiver: %E real, %U user, %S sys." \
		./receiver 7777 test/output_file &
	@/usr/bin/time -f"  Sender: %E real, %U user, %S sys." \
		./sender localhost 7777 test/input_file
	@diff test/input_file output_file

clean:
	@rm -rf test
	@rm -f sender receiver

test-dir:
	@mkdir -p test
