# cpu8 host tools:  make  -> cpu8asm, cpu8sim, cpu8load
CC     ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -O2

TOOLS = cpu8asm cpu8sim

all: $(TOOLS)

cpu8asm: cpu8asm.c cpu8isa.h
	$(CC) $(CFLAGS) -o $@ $<

cpu8sim: cpu8sim.c cpu8isa.h
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TOOLS) cpu8load

.PHONY: all clean
