# Tang Nano 20K build:  make            -> build/tt_cpu.fs
#                       make PROG=../programs/foo.asm
#                       make sim PROG=../programs/hello.asm  (UART output vs cpu8sim)
#                       make flash      (SRAM, lost on power-off)
#                       make flash-persist (SPI flash)
#                       make chip / flash-chip: chip only, for the bench test (docs/bench.md)
OSS    ?= $(HOME)/tools/oss-cad-suite
export PATH := $(OSS)/bin:$(PATH)

PROG   ?= ../programs/blink.asm
DEVICE  = GW2AR-LV18QN88C8/I7
FAMILY  = GW2A-18C
SRCS    = tangnano20k.v uart_out.v uart_in.v ../src/project.v ../src/cpu.v ../model/spi_ram_model.v

all: build/tt_cpu.fs

../tools/cpu8asm: ../tools/cpu8asm.c ../tools/cpu8isa.h
	$(MAKE) -C ../tools cpu8asm

# Always re-assemble (PROG may name a different, older file than last time),
# but only touch prog.hex when it changes, so synthesis reruns only then.
build/prog.hex: FORCE ../tools/cpu8asm | build
	../tools/cpu8asm -f hex -o build/prog.hex.new $(PROG)
	cmp -s build/prog.hex.new $@ || cp build/prog.hex.new $@

FORCE:

build/synth.json: $(SRCS) build/prog.hex
	yosys -q -l build/yosys.log -p "read_verilog -DSYNTH_FPGA $(SRCS); synth_gowin -top tangnano20k_top -json $@"

build/pnr.json: build/synth.json tangnano20k.cst
	nextpnr-himbaechel -q -l build/nextpnr.log --json $< --write $@ \
	  --device $(DEVICE) --vopt family=$(FAMILY) --vopt cst=tangnano20k.cst

build/tt_cpu.fs: build/pnr.json
	gowin_pack -d $(FAMILY) -o $@ $< 2>/dev/null

# ---- chip-only build for the bench test with a Pico 2 (docs/bench.md):
# no RAM and no program in the FPGA, the chip's pins are on the headers.
CHIP_SRCS = tangnano20k_chip.v ../src/project.v ../src/cpu.v

build/chip_synth.json: $(CHIP_SRCS) | build
	yosys -q -l build/chip_yosys.log -p "read_verilog -DSYNTH_FPGA $(CHIP_SRCS); synth_gowin -top tangnano20k_chip -json $@"

build/chip_pnr.json: build/chip_synth.json tangnano20k_chip.cst
	nextpnr-himbaechel -q -l build/chip_nextpnr.log --json $< --write $@ \
	  --device $(DEVICE) --vopt family=$(FAMILY) --vopt cst=tangnano20k_chip.cst

# Some header pins double as configuration pins (SSPI, READY, DONE); the
# flags release them as ordinary I/O, as the LiteX board file for this board does.
build/tt_cpu_chip.fs: build/chip_pnr.json
	gowin_pack -d $(FAMILY) --sspi_as_gpio --mspi_as_gpio --ready_as_gpio --done_as_gpio -o $@ $< 2>/dev/null

chip: build/tt_cpu_chip.fs

flash-chip: build/tt_cpu_chip.fs
	openFPGALoader -b tangnano20k $<

flash-chip-persist: build/tt_cpu_chip.fs
	openFPGALoader -b tangnano20k -f $<

build:
	mkdir -p build

# Simulate the FPGA top: the UART output of two runs (reset with S1 in
# between) must equal cpu8sim's output, twice. The program must halt.
# A "; stdin: text" header in the program is typed on the UART in each run.
STDIN := $(shell sed -n 's/^; *stdin: *//p' $(PROG) | head -1)

sim: build/prog.hex
	printf '%b' '$(STDIN)' > build/stdin.bin
	$(MAKE) -C ../tools cpu8asm cpu8sim
	iverilog -g2005 -DSYNTH_FPGA -o build/tb_fpga.vvp sim/tb_fpga.v $(SRCS)
	vvp -n build/tb_fpga.vvp
	../tools/cpu8asm -o build/prog.bin $(PROG)
	../tools/cpu8sim -a -I '$(STDIN)' build/prog.bin > build/expect1.txt
	cat build/expect1.txt build/expect1.txt > build/expect.txt
	cmp build/expect.txt build/uart.txt
	@echo "UART output matches cpu8sim (2 runs, $$(wc -c < build/uart.txt) bytes)"

flash: build/tt_cpu.fs
	openFPGALoader -b tangnano20k $<

flash-persist: build/tt_cpu.fs
	openFPGALoader -b tangnano20k -f $<

clean:
	rm -rf build

.PHONY: all sim flash flash-persist chip flash-chip flash-chip-persist clean FORCE
