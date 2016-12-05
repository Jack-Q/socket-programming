CC=gcc
CFLAGS=-std=c99 -Wall -g -D_DEFAULT_SOURCE -O3 -DDEBUG -pthread
TEST=./test
SHELL=/bin/bash

SEND=sender
RECV=receiver

.PHONY: all test clean
all: $(SEND) $(RECV)

%: %.c ../common/common.h
	@$(CC) $(CFLAGS) -o $@ $<

test: all | test-dir
	@dd status=none if=/dev/urandom of=test/input_file bs=10MB count=1
	@/usr/bin/time -f"Receiver: %E real, %U user, %S sys." \
		./receiver 7777 test/output_file &
	@sleep 0.05
	@/usr/bin/time -f"  Sender: %E real, %U user, %S sys." \
		./sender localhost 7777 test/input_file
	@diff test/input_file test/output_file

clean:
	@rm -rf test
	@rm -f sender receiver

test-dir:
	@mkdir -p test
