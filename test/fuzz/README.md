# Random-program test

`make` builds the chip Verilog with Verilator and runs random cpu8 programs on it and on the C instruction model (`tools/cpu8core.h`, the core of `cpu8sim`). After **every instruction** it compares A, X, SP, PC, Z, C, the output port and the strobe, and checks the clock count; at the end of each program it compares the whole 4 KB memory. It also resets the CPU at random moments (only while an instruction is being fetched, so no memory write is cut in half).

```sh
make                    # 3000 programs in each MISO sampling mode
make N=20000 SEED=7     # more programs, another seed
make coverage           # coverage of src/cpu.v -> coverage/annotated/cpu.v
build/fall/fuzz -p prog.bin -v    # one program from a file, with a trace
```

Programs come in three kinds, in turn: every byte random; weighted valid instructions with addresses biased towards the stack page, the ends of memory and the code itself; and the second kind with random bytes mixed in. A failing program is saved as `fail_s<seed>_p<n>.bin`.

Results:

- Current design (2026-11-17, with ADC/SBC, the 12-bit X, the input synchroniser and the reset that needs no clock): 6,000 programs, 7.1 million instructions, 17,256 random resets, both sampling modes: no mismatch.
- Coverage of `src/cpu.v` from these programs plus the examples: every line that does something, except the recovery from the unused state codes 6 and 7. No program can reach those; `test_unused_state_recovers` in `test/test.py` forces them.
- Earlier, at commit 7100f2a: 48,000 programs, 58.5 million instructions, 145,568 random resets: no mismatch. Of 12 bugs injected into copies of `cpu.v`, 11 were caught; the twelfth was an equivalent rewrite, not a bug. Neither has been repeated on the current design.

Limits: the RAM model is 4 KB, so addresses past 0xFFF wrap as in the simulator; on a 64 KB RAM a fetch at 0xFFF or a `STRLEN` running off the end reads 0x1000 instead. The test needs Verilator (in the OSS CAD Suite). GitHub's `fuzz` job (in `.github/workflows/test.yaml`) runs a short version, 300 programs in each mode, on every push; the long runs are done by hand.
