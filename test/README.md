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

## Timing simulation results

Run on 2026-11-17 on a local GF180 hardening of commit c322d99 (LibreLane 3.0.14), not on the layout GitHub builds. Every run is all of `test.py` plus all 25 example programs.

| Corner | Clock | MISO_RISE low | MISO_RISE high |
|--------|-------|---------------|----------------|
| slow (`max_ss_125C_3v00`) | 10 MHz | pass | pass |
| slow (`max_ss_125C_3v00`) | 25 MHz | pass | pass |
| typical (`nom_tt_025C_3v30`) | 10 MHz | pass | not run |
| fast (`min_ff_n40C_3v60`) | 10 MHz | pass | not run |
| fast (`min_ff_n40C_3v60`) | 25 MHz | pass | not run |

- The same netlist without delays passes in both MISO_RISE settings.
- At 50 MHz in the slow corner the tests fail, as they should: the delays are really applied.
- Icarus Verilog ignores the setup and hold checks in the SDF file. A path that is too slow shows up as a wrong result in a test, not as a timing message. This simulation adds to the timing report of the hardening run; it does not replace it.
- `test_unused_state_recovers` is skipped on the gate-level netlist: it forces the state register, and the netlist has no signal with that name.

## Waveform

Every run writes `tb.fst`.

```sh
gtkwave tb.fst tb.gtkw
surfer tb.fst
```
