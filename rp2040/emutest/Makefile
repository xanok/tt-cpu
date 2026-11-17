# Replay test of the SPI RAM emulator in rp2040js. See README.md.
#   make setup     once: npm packages, bootrom, test firmware, trace tools
#   make test      every example, both MISO_RISE strap settings
#   make sweep     clock ratio x pad delay table for the three useful
#                  firmware/strap pairings
OSS ?= $(HOME)/tools/oss-cad-suite
ARM ?= $(HOME)/tools/xpack-arm-none-eabi-gcc-15.2.1-1.1
export PATH := $(OSS)/bin:$(ARM)/bin:$(PATH)
FUZZ = ../../test/fuzz
RTL  = $(FUZZ)/fuzz_top.v ../../src/project.v ../../src/cpu.v ../../model/spi_ram_model.v
VFL  = --cc --exe --build -j 0 -O2 --public-flat-rw -Wno-fatal --top-module fuzz_top
BOOTROM_URL = https://raw.githubusercontent.com/wokwi/rp2040js/v1.4.2/demo/bootrom.ts
JOBS ?= 8

setup: node_modules build/bootrom.mjs build/tr_fall/trace build/tr_rise/trace firmware

node_modules: package.json
	npm install --silent

# RP2040 B1 boot ROM (Raspberry Pi, BSD-3-Clause), as shipped with rp2040js
build/bootrom.mjs:
	mkdir -p build
	curl -sSfL -o $@ $(BOOTROM_URL)

build/tr_fall/trace: trace.cpp $(RTL)
	mkdir -p build
	verilator $(VFL) trace.cpp --Mdir build/tr_fall -o trace $(RTL) > build/tr_fall.log 2>&1 || (cat build/tr_fall.log; false)

build/tr_rise/trace: trace.cpp $(RTL)
	mkdir -p build
	verilator $(VFL) -DMISO_RISE -CFLAGS -DMISO_RISE trace.cpp --Mdir build/tr_rise -o trace $(RTL) > build/tr_rise.log 2>&1 || (cat build/tr_rise.log; false)

firmware:
	cmake -S .. -B ../build -DPICO_SDK_PATH=$(HOME)/tools/pico-sdk -DPICOTOOL_FETCH_FROM_GIT_PATH=$(HOME)/tools/picotool > /dev/null
	cmake --build ../build -j8 --target emu_test emu_test_stock | grep -iE "warning|error" || true

test: setup
	./run.sh test $(JOBS)

sweep: setup
	./run.sh sweep $(JOBS)

clean:
	rm -rf build

.PHONY: setup firmware test sweep clean
