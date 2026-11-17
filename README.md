# cpu8: tiny 8-bit CPU for Tiny Tapeout

An 8-bit CPU (accumulator, index register X, stack with CALL/RET) that runs from an external SPI RAM. It is prototyped on a Tang Nano 20K and targets a Tiny Tapeout submission.

## Documentation

| Document | What is in it |
|----------|---------------|
| [docs/isa.md](docs/isa.md) | instruction set, stack, timing, assembler syntax |
| [docs/io.md](docs/io.md) | input and output: `OUT`, `IN`, text output, keyboard input, on every platform |
| [programs/README.md](programs/README.md) | the 23 example programs and the two libraries |
| [rp2040/README.md](rp2040/README.md) | demo board firmware for the RP2350B and RP2040 boards: build, pin maps, serial protocol, timing limits |
| [docs/bench.md](docs/bench.md) | hardware test before tapeout: Tang Nano 20K as the chip, Pico 2 as the demo board, wiring table |
| [docs/info.md](docs/info.md) | the Tiny Tapeout datasheet page: pins, strap, reset |

## Quick start

```sh
make -C tools                                    # build cpu8asm, cpu8sim, cpu8load
tools/cpu8asm -o hello.bin programs/hello.asm
tools/cpu8sim -a hello.bin                       # Hello, Tiny Tapeout!

tools/cpu8asm -o ask.bin programs/ask.asm
tools/cpu8sim -a -k ask.bin                      # asks your name and age
```

The same programs run in three places:

| Where | Run | Text output | Keyboard input |
|-------|-----|-------------|----------------|
| Simulator | `tools/cpu8sim -a prog.bin` | terminal | `-k` |
| TT demo board (firmware from `rp2040/`, for the RP2350B or RP2040 board) | `tools/cpu8load -a prog.bin` | terminal | `-k` |
| Tang Nano 20K | `cd fpga && make flash PROG=../programs/prog.asm` | serial terminal, 115200 baud | same serial terminal |

Nothing has run on real hardware yet. Everything is verified in simulation and against a mock of the demo board.

## Repository layout

```
src/            chip RTL (only these go to tapeout)
  project.v       tt_um_xanok_cpu8 wrapper (TT pinout)
  cpu.v           the CPU core + SPI master
model/          SPI RAM model: testbench memory AND on-FPGA block RAM
tools/          C tools: cpu8asm (assembler), cpu8sim (simulator), cpu8load (demo board loader)
programs/       23 example programs, lib/print.asm and lib/input.asm, run_tests.sh
rp2040/         demo board firmware (RP2350B and RP2040): SPI RAM emulator + USB loader; mock/ tests it on a PC
test/           cocotb tests (TT CI runs these)
fpga/           Tang Nano 20K top, UART in/out, pin constraints, Makefile
docs/           isa.md, io.md, info.md
```

## Commands

Toolchain for the Verilog tests and the FPGA: `source ~/tools/oss-cad-suite/environment`

**Programs**

| Task | Command |
|---|---|
| Build the C tools | `make -C tools` |
| Assemble | `tools/cpu8asm -o prog.bin -l prog.lst prog.asm` |
| Simulate | `tools/cpu8sim -s prog.bin` (`-a` text output, `-k` keyboard input, `-i` input port, `-t` trace, `-d addr:len` dump) |
| Run on the demo board | `tools/cpu8load -a prog.bin` (`-k` keyboard input, `-n` numbers, `-i` input port, `-r addr:len` dump RAM) |

**Tests**

| Task | Command |
|---|---|
| Examples in the simulator | `programs/run_tests.sh` |
| Chip Verilog, including every example against the simulator | `cd test && make` (waveform: `gtkwave tb.fst`) |
| Same, with the MISO_RISE strap high and stock spi-ram-emu RAM timing | `cd test && make MISO_RISE=yes` |
| Random programs on the Verilog against the instruction model, both sampling modes | `cd test/fuzz && make` (`N=20000 SEED=7` for more; `make coverage` for line/toggle coverage) |
| Loader and firmware protocol on the mock demo board | `cd rp2040/mock && make test` |
| FPGA top: UART output against the simulator | `cd fpga && make sim PROG=../programs/hello.asm` |

**FPGA**

| Task | Command |
|---|---|
| Build the bitstream | `cd fpga && make` (or `make PROG=../programs/other.asm`) |
| Load into SRAM (lost on power-off) | `cd fpga && make flash` |
| Load permanently | `cd fpga && make flash-persist` |

## Running an example on the Tang Nano 20K

The FPGA holds the whole system: the CPU, plus the SPI RAM as block RAM preloaded with the program. The program is chosen when you build the bitstream.

```sh
source ~/tools/oss-cad-suite/environment
openFPGALoader --detect                          # once: board found? (GW2AR-18)
cd fpga
make flash PROG=../programs/hello.asm            # build + load (lost on power-off)
picocom -b 115200 /dev/ttyUSB1                   # text output (2nd port of the board)
```

- **LEDs:** show `uo_out[5:0]`, the last `OUT` value. `knight_rider.asm` and `blink.asm` are made for them.
- **S1:** reset. Press it to run the program again, for example after opening the terminal.
- **S2:** `ui_in[1]`. `ui_in[0]` reads 0.
- **UART:** every byte written with `OUT` goes over the board's USB-C cable at 115200 8N1 (FPGA pin 69), so `hello`, `print_demo`, `squares` and `hanoi` print text. A 1 KB FIFO absorbs bursts; when it is full, bytes are dropped.
- **Keyboard input:** what you type in the terminal goes to the FPGA (pin 70) and on to the program, so `ask.asm` works. Start picocom with `--echo --omap crlf` and press S1 to see the first prompt. Details are in [docs/io.md](docs/io.md).
- The board shows up as two serial ports. Use the second one, usually `/dev/ttyUSB1`. If `openFPGALoader` reports "permission denied", the USB device needs a udev rule, which needs root.

## Target process and shuttle

The chip targets **GlobalFoundries GF180MCU** on the Tiny Tapeout shuttle **ttgf26c**, in a 1x1 tile (about 340 × 160 µm).

| | GF180 (this branch) |
|---|---|
| Utilization | 68% |
| Clock target | 25 MHz (`CLOCK_PERIOD` 40 ns); the chip is meant to run at 10 MHz |
| Worst setup / hold slack | +3.6 ns / +0.20 ns over all corners (local run, LibreLane 3.0.14) |
| Special setting | delay cells excluded (`EXTRA_EXCLUDED_CELLS`), see `src/config.json` |

The design was first hardened for SkyWater sky130 (1x1 tile, 82% utilization, shuttle tag `ttsky26d`). That state, with its config and its metal-4 logo macro, is kept as the git tag **`sky130-verified`**. The silicon-art logo is regenerated for GF180 Metal4 (`macros/`, from `art/` with `tools/make_logo.py --pdk gf180mcuD`); the sky130 version of the macro is in the tag.

## Before submitting to Tiny Tapeout

- Make the repo public (TT needs public GitHub Actions) and turn on GitHub Pages.
- Fill in `author` / `discord` in `info.yaml`.
- Push to GitHub. The `gds` action hardens the design and shows whether it fits the tile count.
- Submit the commit whose `gds` run passed.
