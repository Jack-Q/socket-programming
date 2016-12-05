###########################################################
# Makefile for Network Programming Homework 2
# Jack Q (qiaobo@outlook.com) / 0540017, CS, NCTU
###########################################################

CC=gcc
CFLAGS=-std=c99 -Wall -g -D_DEFAULT_SOURCE -O3 -pthread

PROG_01_DIR=01-signal-alarm
PROG_02_DIR=02-select
PROG_03_DIR=03-socket-option

FILESIZE=5MB

.PHONY: all Program-01 Program-02 Program-03 clean test
all: Program-01 Program-02 Program-03

Program-01:
	@make --no-print-directory -C $(PROG_01_DIR) CC="$(CC)" CFLAGS="$(CFLAGS)"

Program-02:
	@make --no-print-directory -C $(PROG_02_DIR) CC="$(CC)" CFLAGS="$(CFLAGS)"

Program-03:
	@make --no-print-directory -C $(PROG_03_DIR) CC="$(CC)" CFLAGS="$(CFLAGS)"

clean:
	@make --no-print-directory -C $(PROG_01_DIR) clean
	@make --no-print-directory -C $(PROG_02_DIR) clean
	@make --no-print-directory -C $(PROG_03_DIR) clean

test:
	@make --no-print-directory -C $(PROG_01_DIR) test FILESIZE=$(FILESIZE)
	@make --no-print-directory -C $(PROG_02_DIR) test FILESIZE=$(FILESIZE)
	@make --no-print-directory -C $(PROG_03_DIR) test FILESIZE=$(FILESIZE)
