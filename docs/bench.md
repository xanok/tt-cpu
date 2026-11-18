# Bench test: Tang Nano 20K + Raspberry Pi Pico 2

A test of the whole system on real hardware before tapeout, without a Tiny Tapeout chip:

- the **Tang Nano 20K** holds the chip's Verilog and nothing else (no RAM, no program);
- the **Pico 2** runs the demo board firmware and plays the demo board: it makes the clock and reset, emulates the SPI RAM, captures the output and talks to `cpu8load` over USB.

This exercises what simulation cannot: the SPI link over real pins, the RAM emulator on a real RP2350, both MISO strap settings, the loader, and keyboard input.

**Status: prepared, not yet run.** The firmware and the bitstream build. Check the two pin tables against the boards before connecting anything.

Done on hardware so far (2026-11-18, Pico 2 only, no Tang Nano yet): the loader firmware runs on the Pico 2, answers `cpu8load` over USB, and loads and reads back the emulated RAM.

**Next, before the Tang Nano arrives:** the loopback test of the RAM emulator on the Pico 2 alone, with four jumper wires on the same board. It is built and was run once without the wires (the waveform plays at 10 MHz on time, the checks work); it has not been run with the wires. See [the loopback test](../rp2040/emutest/README.md#on-a-real-pico-2) for the wiring and the commands. The Pico 2 currently holds the loopback firmware: send `BOOT` to it (or hold BOOTSEL while plugging it in) and flash `cpu8_loader.uf2` before the bench test.

## What you need

- Tang Nano 20K with header pins, and its USB-C cable
- Raspberry Pi Pico 2 (RP2350) with header pins, and a micro-USB data cable
- 21 jumper wires (female-female if both boards have male pins): 20 signals and ground

Both boards use 3.3 V I/O. Each is powered by its own USB cable. **Connect the grounds, never the 3V3 or 5V pins.**

## Wiring

Pico pins are named as on the Pico's silkscreen (GP numbers). Tang Nano pins are FPGA pin numbers, which are printed on the back of the board. The header position column comes from the LiteX board file for the Tang Nano 20K and is only a guide: go by the printed pin number.

| Signal | Direction | Pico 2 | Tang Nano 20K pin | Header position |
|--------|-----------|--------|-------------------|-----------------|
| ground | | any GND | any GND | |
| clk | Pico → FPGA | GP26 | 73 | J6-1 |
| rst_n | Pico → FPGA | GP27 | 74 | J6-2 |
| SPI CS_N (uio0) | FPGA → Pico | GP16 | 55 | J5-11 |
| SPI MOSI (uio1) | FPGA → Pico | GP17 | 49 | J5-12 |
| SPI MISO (uio2) | Pico → FPGA | GP18 | 86 | J5-13 |
| SPI SCK (uio3) | FPGA → Pico | GP19 | 72 | J5-17 |
| HALTED (uio4) | FPGA → Pico | GP20 | 71 | J5-18 |
| OUT_STROBE (uio5) | FPGA → Pico | GP21 | 53 | J5-19 |
| MISO_RISE strap (uio6) | Pico → FPGA | GP22 | 52 | J5-20 |
| uo_out[0] | FPGA → Pico | GP8 | 75 | J5-3 |
| uo_out[1] | FPGA → Pico | GP9 | 80 | J5-4 |
| uo_out[2] | FPGA → Pico | GP10 | 42 | J5-5 |
| uo_out[3] | FPGA → Pico | GP11 | 41 | J5-6 |
| uo_out[4] | FPGA → Pico | GP12 | 56 | J5-7 |
| uo_out[5] | FPGA → Pico | GP13 | 54 | J5-8 |
| uo_out[6] | FPGA → Pico | GP14 | 51 | J5-9 |
| uo_out[7] | FPGA → Pico | GP15 | 48 | J5-10 |
| ui_in[0] (optional) | Pico → FPGA | GP0 | 76 | J6-3 |
| ui_in[1] (optional) | Pico → FPGA | GP1 | 85 | J6-4 |
| ui_in[2] (optional) | Pico → FPGA | GP2 | 77 | J6-5 |

- **Start with the first ten rows** (ground, clk, rst_n and uio0-6). With only those, programs run and halt, and `cpu8load -w` reports it. Add uo_out for text output, and ui_in only for the `IN` instruction.
- **ui_in[7:3] are not wired.** They read 0 in this build. The Tang Nano pins left over are either LEDs or pins whose voltage I could not confirm, so they are not used.
- **Keep the SPI wires short** (10 cm) and of similar length. SCK runs at 5 MHz.
- The Tang Nano's six LEDs show `uo_out[5:0]`.

## Steps

```sh
# 1. FPGA: the chip only
source ~/tools/oss-cad-suite/environment
cd fpga && make flash-chip            # lost on power-off; "make flash-chip-persist" keeps it

# 2. Pico 2: the demo board firmware with the bench pin map
export PATH=~/tools/xpack-arm-none-eabi-gcc-15.2.1-1.1/bin:$PATH
cd ../rp2040
cmake -B build_pico2 -DTT_BOARD=pico2 -DPICO_SDK_PATH=$HOME/tools/pico-sdk -DPICOTOOL_FETCH_FROM_GIT_PATH=$HOME/tools/picotool
cmake --build build_pico2 -j8 --target cpu8_loader
# hold BOOTSEL on the Pico while plugging it in, then copy the file to the drive that appears
cp build_pico2/cpu8_loader.uf2 /media/$USER/RP2350/

# 3. Run programs (no -p: there is no project to select on the bench)
cd .. && make -C tools
tools/cpu8asm -o hello.bin programs/hello.asm
tools/cpu8load -f 1000000 -w hello.bin        # first try: slow clock, just wait for HLT
tools/cpu8load -a hello.bin                   # 10 MHz, text output
tools/cpu8asm -o ask.bin programs/ask.asm
tools/cpu8load -k ask.bin                     # keyboard input
```

The Pico appears as `/dev/ttyACM0` (use `-d` for another name). The Tang Nano's own serial port is not used in this test.

On Linux the serial port belongs to the `dialout` group. If `cpu8load` says "Permission denied", run `sudo usermod -aG dialout $USER` once and log in again (until then, `sg dialout -c "tools/cpu8load ..."` works).

## What to try, in order

1. `cpu8load -f 1000000 -w hello.bin` reports `halted = yes`. This proves the clock, reset, the SPI link and the RAM emulator at an easy speed.
2. The same at the default 10 MHz.
3. `-a` prints "Hello, Tiny Tapeout!": output capture works.
4. Every example against the simulator: `tools/cpu8load -n prog.bin` must print the same numbers as `tools/cpu8sim prog.bin`.
5. `-e 1`: the other MISO sampling edge. With this firmware's RAM timing both settings should work at 10 MHz.
6. `-k ask.bin`: keyboard input through the mailbox.
7. Find the limit: raise `-f` until it fails (the firmware refuses more than 15 MHz at its 150 MHz system clock; `-s 200000` raises that to 20 MHz).

## If it does not work

- **No answer from the device:** wrong serial port, or the Pico is not running the firmware.
- **Timeout, CPU did not halt:** check ground, clk, rst_n and the four SPI wires first. Try `-f 100000`. Then try `-e 1`.
- **Halts but the output is wrong or missing:** check the uo_out wires and OUT_STROBE.
- **Works at 1 MHz but not at 10 MHz:** shorten the SPI wires; try `-e 1`; try `-s 200000`.

## Differences from the real demo board

- The real board has an RP2350B with other pin numbers (`-DTT_BOARD=rp2350`); the Pico 2 has the smaller RP2350A. The emulator code and its relative pin order are the same.
- There is no project multiplexer here. On the real chip the signals also pass through the Tiny Tapeout multiplexer, which adds delay; jumper wires add their own.
- The FPGA is not the GF180 chip: it proves the logic and the protocol, not the silicon timing.
