# cpu8: tiny 8-bit CPU for Tiny Tapeout

An 8-bit accumulator CPU that runs from an external SPI RAM. It is prototyped on a Tang Nano 20K and targets a Tiny Tapeout submission.

```
src/            chip RTL (only these go to tapeout)
  project.v       tt_um_xanok_cpu8 wrapper (TT pinout)
  cpu.v           the CPU core + SPI master
model/          SPI RAM model: testbench memory AND on-FPGA block RAM
tools/asm.py    assembler  (asm.py prog.asm -o prog.hex)
programs/       example programs
test/           cocotb tests (TT CI runs these)
fpga/           Tang Nano 20K top, pin constraints, Makefile
docs/           isa.md (instruction set), info.md (TT datasheet page)
```

Toolchain: `source ~/tools/oss-cad-suite/environment`

| Task | Command |
|---|---|
| Run tests | `cd test && make` (waveform in `test/tb.fst`: `gtkwave tb.fst`) |
| Build FPGA bitstream | `cd fpga && make` (or `make PROG=../programs/other.asm`) |
| Flash to SRAM (temporary) | `cd fpga && make flash` |
| Flash permanently | `cd fpga && make flash-persist` |

On the Tang Nano 20K the 6 LEDs show `uo_out[5:0]`, and buttons S1/S2 feed `ui_in[1:0]`.

## Before submitting to Tiny Tapeout

- Make the repo public (TT needs public GitHub Actions).
- Fill in `author` / `discord` in `info.yaml`.
- Push to GitHub. The `gds` action hardens the design and shows whether it fits the tile count.
- Decide how the program gets into the SPI RAM on the real board.
