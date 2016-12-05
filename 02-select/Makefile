###########################################################
# Makefile for Network Programming Homework 2
# Jack Q (qiaobo@outlook.com) / 0540017, CS, NCTU
###########################################################


CC=gcc
CFLAGS=-std=c99 -Wall -g -D_DEFAULT_SOURCE -O3 -DDEBUG -pthread
TEST=./test
SHELL=/bin/bash

FILESIZE=5MB

SEND=sender
RECV=receiver

.PHONY: all test clean
all: $(SEND) $(RECV)

%: %.c ../common/common.h
	@$(CC) $(CFLAGS) -o $@ $<

test: all | test-dir
	@echo
	@echo "#### Test Program 02:select with file size $(FILESIZE) ####"
	@dd status=none if=/dev/urandom of=test/input_file bs=$(FILESIZE) count=1
	@/usr/bin/time -f"Receiver: %E real, %U user, %S sys." \
		./receiver 7777 test/output_file &
	@sleep 0.05
	@/usr/bin/time -f"  Sender: %E real, %U user, %S sys." \
		./sender localhost 7777 test/input_file
  # Wait server finish
	@sleep 2
	@diff test/input_file test/output_file

clean:
	@rm -rf test
	@rm -f sender receiver

test-dir:
	@mkdir -p test
