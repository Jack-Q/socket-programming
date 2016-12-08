###########################################################
# Makefile for Network Programming Homework 2
# Jack Q (qiaobo@outlook.com) / 0540017, CS, NCTU
###########################################################

CC=gcc
CFLAGS=-std=c99 -Wall -g -D_DEFAULT_SOURCE -O3 -pthread
TEST=./test
SHELL=/bin/bash

FILESIZE=5M

SEND=sender
RECV=receiver

.PHONY: all test clean
all: $(SEND) $(RECV)

%: %.c ../common/common.h
	@$(CC) $(CFLAGS) -o $@ $<

test: all
	@echo
	@echo "#### Test Program 01:alarm with file size $(FILESIZE) ####"
	@mkdir -p test
	@dd status=none if=/dev/urandom of=test/input_file bs=$(FILESIZE) count=1
	@echo Receiver `time \
		./receiver 7777 test/output_file `&
	@sleep 0.05
	@echo Sender `time \
		./sender localhost 7777 test/input_file`
	@sleep 1
	@diff test/input_file test/output_file

clean:
	@rm -rf test
	@rm -f sender receiver
