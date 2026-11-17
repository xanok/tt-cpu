# Tang Nano 20K build:  make            -> build/tt_cpu.fs
#                       make PROG=../programs/foo.asm
#                       make flash      (SRAM, lost on power-off)
#                       make flash-persist (SPI flash)
OSS    ?= $(HOME)/tools/oss-cad-suite
export PATH := $(OSS)/bin:$(PATH)

PROG   ?= ../programs/blink.asm
DEVICE  = GW2AR-LV18QN88C8/I7
FAMILY  = GW2A-18C
SRCS    = tangnano20k.v ../src/project.v ../src/cpu.v ../model/spi_ram_model.v

all: build/tt_cpu.fs

../tools/cpu8asm: ../tools/cpu8asm.c ../tools/cpu8isa.h
	$(MAKE) -C ../tools cpu8asm

build/prog.hex: $(PROG) ../tools/cpu8asm | build
	../tools/cpu8asm -f hex -o $@ $(PROG)

build/synth.json: $(SRCS) build/prog.hex
	yosys -q -l build/yosys.log -p "read_verilog -DSYNTH_FPGA $(SRCS); synth_gowin -top tangnano20k_top -json $@"

build/pnr.json: build/synth.json tangnano20k.cst
	nextpnr-himbaechel -q -l build/nextpnr.log --json $< --write $@ \
	  --device $(DEVICE) --vopt family=$(FAMILY) --vopt cst=tangnano20k.cst

build/tt_cpu.fs: build/pnr.json
	gowin_pack -d $(FAMILY) -o $@ $< 2>/dev/null

build:
	mkdir -p build

flash: build/tt_cpu.fs
	openFPGALoader -b tangnano20k $<

flash-persist: build/tt_cpu.fs
	openFPGALoader -b tangnano20k -f $<

clean:
	rm -rf build

.PHONY: all flash flash-persist clean
