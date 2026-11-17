# Example programs

Each program's header comments hold its expected output. `./run_tests.sh` assembles every program with `cpu8asm`, runs it in `cpu8sim` and compares the output with those comments. `cd ../test && make` also runs each halting program on the Verilog RTL. It checks that the OUT values, the full memory and the exact clock count all match the simulator.

| Program | Shows |
|---------|-------|
| `hello.asm` | string output with `LDA msg,X` |
| `count.asm` | simplest loop, `CPI` |
| `sum_array.asm` | `ADD ea,X`, 16-bit sum with carry |
| `add16.asm` | `ADC` / `SBC`, 16-bit add and subtract without a branch |
| `pointer.asm` | 12-bit X as a pointer: table of string addresses, 300-byte block |
| `fibonacci.asm` | stop on carry |
| `multiply.asm` | 8×8→16 shift-and-add subroutine (`CALL`/`RET`) |
| `divide.asm` | restoring division subroutine; saves X with `PUSH`/`POP` |
| `primes.asm` | sieve of Eratosthenes with `STA sieve,X` |
| `bubble_sort.asm` | in-place sort with indexed loads/stores |
| `strlen.asm` | hardware `STRLEN` vs a software loop |
| `reverse.asm` | reverse a string through the stack |
| `gcd.asm` | Euclid's algorithm as a subroutine |
| `factorial.asm` | recursion, with `n` saved on the stack |
| `squares.asm` | decimal printing with `print_dec` from the library |
| `print_demo.asm` | the print library: strings, decimal, hex, repeated characters |
| `memcpy.asm` | block copy across a page boundary, verify, checksum |
| `ask.asm` | keyboard input: asks your name and age, then prints both (`getline`) |
| `bits.asm` | reads the input port: popcount, parity, bit reverse |
| `maxmin.asm` | maximum and minimum, and their positions |
| `hanoi.asm` | Towers of Hanoi, recursion with 4 saved arguments |
| `collatz.asm` | 3n+1 sequence |
| `lfsr.asm` | Galois LFSR pseudo-random numbers |
| `blink.asm` | endless LED counter (FPGA demo) |
| `knight_rider.asm` | endless LED scanner with a delay subroutine |

```sh
../tools/cpu8asm -o hello.bin hello.asm
../tools/cpu8sim -a hello.bin          # Hello, Tiny Tapeout!
../tools/cpu8sim -s -t factorial.bin   # trace + register/clock summary
../tools/cpu8load -a hello.bin         # on the demo board
```

```sh
../tools/cpu8asm -o ask.bin ask.asm
../tools/cpu8sim -a -k ask.bin         # interactive, in the simulator
../tools/cpu8load -k ask.bin           # interactive, on the demo board
```

## Libraries

| File | Routines | For |
|------|----------|-----|
| `lib/print.asm` | `print`, `print_dec`, `print_hex`, `newline` | text output |
| `lib/input.asm` | `getchar`, `getline` | keyboard input |

Include them with `.include "lib/print.asm"` after your program's last instruction. [docs/io.md](../docs/io.md) explains how input and output work and how to run interactive programs on each platform.
