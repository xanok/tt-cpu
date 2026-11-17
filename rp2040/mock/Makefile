# Host build of the firmware command parser + mock hardware, and the loader.
CC     ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -O2

all: cpu8mock cpu8load

cpu8mock: mock_main.c hw_mock.c ../src/proto.c ../src/proto.h ../src/hw.h
	$(CC) $(CFLAGS) -I../src -o $@ mock_main.c hw_mock.c ../src/proto.c

cpu8load: ../../tools/cpu8load.c
	$(CC) $(CFLAGS) -o $@ $<

test: all
	./test.sh

clean:
	rm -f cpu8mock cpu8load

.PHONY: all test clean
