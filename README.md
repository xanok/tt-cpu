# cpu8: tiny 8-bit CPU for Tiny Tapeout

An 8-bit CPU (accumulator, index register X, stack with CALL/RET) that runs from an external SPI RAM. It is prototyped on a Tang Nano 20K and targets a Tiny Tapeout submission.

```
src/            chip RTL (only these go to tapeout)
  project.v       tt_um_xanok_cpu8 wrapper (TT pinout)
  cpu.v           the CPU core + SPI master
model/          SPI RAM model: testbench memory AND on-FPGA block RAM
tools/          C tools: cpu8asm (assembler), cpu8sim (simulator), cpu8load (RP2040 loader)
programs/       22 example programs, lib/print.asm (text output), run_tests.sh
rp2040/         RP2040 firmware: SPI RAM emulator + USB loader
test/           cocotb tests (TT CI runs these)
fpga/           Tang Nano 20K top, pin constraints, Makefile
docs/           isa.md (instruction set), info.md (TT datasheet page)
```

Toolchain: `source ~/tools/oss-cad-suite/environment`

| Task | Command |
|---|---|
| Build the C tools | `make -C tools` |
| Assemble | `tools/cpu8asm -o prog.bin -l prog.lst prog.asm` |
| Simulate | `tools/cpu8sim -s prog.bin` (`-a` text output, `-t` trace, `-i` input port, `-k` keyboard input, `-d addr:len` dump) |
| Test the examples in the simulator | `programs/run_tests.sh` |
| Run on the demo board (RP2040 firmware in `rp2040/`) | `tools/cpu8load -a prog.bin` (text output), `-k` (text output and keyboard input), `-n` (numbers), `-r addr:len` (dump RAM) |
| Run tests (RTL, incl. every example vs the simulator) | `cd test && make` (waveform in `test/tb.fst`: `gtkwave tb.fst`) |
| Same, with the MISO_RISE strap high and stock spi-ram-emu RAM timing | `cd test && make MISO_RISE=yes` |
| Build FPGA bitstream | `cd fpga && make` (or `make PROG=../programs/other.asm`) |
| Flash to SRAM (temporary) | `cd fpga && make flash` |
| Flash permanently | `cd fpga && make flash-persist` |
| Simulate the FPGA top (UART output vs cpu8sim; a `; stdin:` header is typed on the UART) | `cd fpga && make sim PROG=../programs/hello.asm` |

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
- **Keyboard input:** what you type in the terminal goes to the FPGA (pin 70) and into the input mailbox, so programs that use `lib/input.asm` work: `make PROG=../programs/ask.asm flash`. Two things differ from `cpu8load -k`. Start picocom with `--echo --omap crlf`, which shows what you type and sends Enter as a newline. And the program starts at power-on, so its first prompt is printed before the terminal is open; press S1 to see it. The receive FIFO holds 64 bytes.
- The board shows up as two serial ports. Use the second one, usually `/dev/ttyUSB1`. If `openFPGALoader` reports "permission denied", the USB device needs a udev rule, which needs root.

## Before submitting to Tiny Tapeout

- Make the repo public (TT needs public GitHub Actions).
- Fill in `author` / `discord` in `info.yaml`.
- Push to GitHub. The `gds` action hardens the design and shows whether it fits the tile count.
