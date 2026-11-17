# cpu8 host tools:  make  -> cpu8asm, cpu8sim, cpu8dis, cpu8load
CC     ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -O2

TOOLS = cpu8asm cpu8sim cpu8dis cpu8load

all: $(TOOLS)

cpu8asm: cpu8asm.c cpu8isa.h
	$(CC) $(CFLAGS) -o $@ $<

cpu8sim: cpu8sim.c cpu8core.h cpu8isa.h
	$(CC) $(CFLAGS) -o $@ $<

cpu8dis: cpu8dis.c cpu8isa.h
	$(CC) $(CFLAGS) -o $@ $<

cpu8load: cpu8load.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TOOLS)

.PHONY: all clean
