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
| Simulate | `tools/cpu8sim -s prog.bin` (`-a` text output, `-t` trace, `-i` input, `-d addr:len` dump) |
| Test the examples in the simulator | `programs/run_tests.sh` |
| Run on the demo board (RP2040 firmware in `rp2040/`) | `tools/cpu8load -a prog.bin` (text output), `-n` (numbers), `-r addr:len` (dump RAM) |
| Run tests (RTL, incl. every example vs the simulator) | `cd test && make` (waveform in `test/tb.fst`: `gtkwave tb.fst`) |
| Build FPGA bitstream | `cd fpga && make` (or `make PROG=../programs/other.asm`) |
| Flash to SRAM (temporary) | `cd fpga && make flash` |
| Flash permanently | `cd fpga && make flash-persist` |

On the Tang Nano 20K the 6 LEDs show `uo_out[5:0]`, and buttons S1/S2 feed `ui_in[1:0]`.

## Before submitting to Tiny Tapeout

- Make the repo public (TT needs public GitHub Actions).
- Fill in `author` / `discord` in `info.yaml`.
- Push to GitHub. The `gds` action hardens the design and shows whether it fits the tile count.
