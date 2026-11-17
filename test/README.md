# Tests of the chip Verilog

cocotb tests, run with Icarus Verilog. The Tiny Tapeout GitHub workflow runs them on every push.

| File | What it tests |
|------|---------------|
| `test.py` | one small program per instruction or feature; checks outputs and memory after `HLT`, and reset |
| `test_programs.py` | every `programs/*.asm` on the Verilog against the C simulator: the `OUT` values, the 4 KB memory and the exact clock count |
| `tb.v` | the testbench: the chip (`src/`) connected to the SPI RAM model (`model/spi_ram_model.v`) |
| `fuzz/` | random programs against the instruction model, with Verilator; see [fuzz/README.md](fuzz/README.md) |

## Running

Tools: `source ~/tools/oss-cad-suite/environment`, and `make -C ../tools` for the assembler and simulator.

| Task | Command |
|------|---------|
| Verilog source (RTL) | `make` |
| Same, with the MISO_RISE strap high and a RAM with stock spi-ram-emu timing | `make MISO_RISE=yes` |
| Only some example programs | `make PROGRAMS=hello,primes` |
| Gate-level netlist | `make GATES=yes PDK_ROOT=<pdk>` |
| Gate-level netlist with the delays from the layout | `make GATES=yes SDF=<file>.sdf PDK_ROOT=<pdk>` |
| Another clock period (default 100 ns = 10 MHz) | add `CLK_NS=40` |

`make` reports success even when a test fails. Check the `TESTS= PASS= FAIL=` line, or run `python -m cocotb_tools.check_results results.xml`.

## Gate-level netlist

Harden the design first, then copy `runs/wokwi/final/pnl/tt_um_xanok_cpu8.pnl.v` to `gate_level_netlist.v` in this folder. For the timing simulation, take the SDF file from `runs/wokwi/final/sdf/<corner>/` of the same run.

## Waveform

Every run writes `tb.fst`.

```sh
gtkwave tb.fst tb.gtkw
surfer tb.fst
```
