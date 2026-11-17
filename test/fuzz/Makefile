# Random-program test of the chip Verilog against the C instruction model.
#   make            build and run both sampling modes (N programs each)
#   make N=20000 SEED=7
#   make coverage   line/toggle coverage of src/cpu.v from the random
#                   programs plus every example program -> coverage/
OSS ?= $(HOME)/tools/oss-cad-suite
export PATH := $(OSS)/bin:$(PATH)
N    ?= 3000
SEED ?= 1
SRCS  = fuzz_top.v ../../src/project.v ../../src/cpu.v ../../model/spi_ram_model.v
VFLAGS = --cc --exe --build -j 0 -O2 --public-flat-rw --coverage -Wno-fatal \
         --top-module fuzz_top -CFLAGS "-I$(abspath ../../tools) -O2" fuzz.cpp

all: run

build/fall/fuzz: $(SRCS) fuzz.cpp ../../tools/cpu8core.h
	mkdir -p build
	verilator $(VFLAGS) --Mdir build/fall -o fuzz $(SRCS) > build_fall.log 2>&1 || (cat build_fall.log; false)

build/rise/fuzz: $(SRCS) fuzz.cpp ../../tools/cpu8core.h
	mkdir -p build
	verilator $(VFLAGS) -DMISO_RISE --Mdir build/rise -o fuzz $(SRCS) > build_rise.log 2>&1 || (cat build_rise.log; false)

run: build/fall/fuzz build/rise/fuzz
	build/fall/fuzz -n $(N) -s $(SEED)
	build/rise/fuzz -n $(N) -s $(SEED)

coverage: build/fall/fuzz build/rise/fuzz
	$(MAKE) -s -C ../../tools cpu8asm
	mkdir -p coverage/bin
	build/fall/fuzz -n $(N) -s $(SEED) -c coverage/fuzz_fall.dat
	build/rise/fuzz -n $(N) -s $(SEED) -c coverage/fuzz_rise.dat
	for f in ../../programs/*.asm; do \
	  grep -q '^; *limit:' $$f && continue; grep -q '^; *stdin:' $$f && continue; \
	  n=$$(basename $$f .asm); in=$$(sed -n 's/^; *input: *//p' $$f | head -1); \
	  ../../tools/cpu8asm -o coverage/bin/$$n.bin $$f && \
	  build/fall/fuzz -p coverage/bin/$$n.bin -i $${in:-0} -c coverage/ex_$$n.dat || exit 1; \
	done
	verilator_coverage --write coverage/all.dat coverage/*.dat
	verilator_coverage --annotate coverage/annotated --annotate-min 1 coverage/all.dat

clean:
	rm -rf build coverage *.log fail_*.bin

.PHONY: all run coverage clean
