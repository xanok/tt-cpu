# cpu8 instruction set

Registers:
- `A`: 8-bit accumulator;
- `X`: 8-bit index register;
- `SP`: 8-bit stack pointer;
- `PC`: 12-bit program counter;
- flags `Z` (zero) and `C` (carry / borrow).

Every instruction is 2 bytes: `[op:4 | hi:4] [lo:8]`. `ea = {hi, lo}` is a 12-bit address and `imm = lo`.

Code, data and the stack share one 4 KB address space in the external SPI RAM. Execution starts at `0x000` after reset, with all registers at 0.

## Opcodes

| op | Mnemonic | Effect | Flags |
|----|----------|--------|-------|
| 0 | immediate group, selected by `hi` (below) | | |
| 1 | `LDA ea`    | A = M[ea] | Z |
| 2 | `STA ea`    | M[ea] = A | – |
| 3 | `ADD ea`    | A = A + M[ea] | Z, C = carry |
| 4 | `SUB ea`    | A = A − M[ea] | Z, C = borrow |
| 5 | `LDA ea,X`  | A = M[ea + X] | Z |
| 6 | `STA ea,X`  | M[ea + X] = A | – |
| 7 | `ADD ea,X`  | A = A + M[ea + X] | Z, C |
| 8 | `CALL ea`   | SP −= 2, M[SP] = return high, M[SP+1] = return low, PC = ea | – |
| 9 | `JMP ea`    | PC = ea | – |
| A | `JZ ea`     | if Z: PC = ea | – |
| B | `JC ea`     | if C: PC = ea | – |
| C | `JNZ ea`    | if !Z: PC = ea | – |
| D | misc group, selected by `hi` (below) | | |
| E | `CMP ea`    | flags of A − M[ea], A unchanged | Z, C |
| F | `STRLEN ea` | A = length of the NUL-terminated string at ea (max 255) | Z = empty, C = no NUL within 255 bytes |

`ea + X` is a 12-bit add, so an indexed access can cross a 256-byte page. It wraps at 4 KB.

### Immediate group (op 0)

| code | Mnemonic | Effect | Flags |
|------|----------|--------|-------|
| 00 | `LDI imm` | A = imm | Z |
| 01 | `ADI imm` | A = A + imm (`ADI -1` decrements) | Z, C |
| 02 | `SBI imm` | A = A − imm | Z, C = borrow |
| 03 | `ANI imm` | A = A & imm | Z |
| 04 | `ORI imm` | A = A \| imm | Z |
| 05 | `XRI imm` | A = A ^ imm | Z |
| 06 | `CPI imm` | flags of A − imm | Z, C |
| 07 | `LDX imm` | X = imm | Z |
| 08 | `CPX imm` | flags of X − imm | Z, C |
| 09–0F | reserved | does nothing | – |

### Misc group (op D, operand byte ignored)

| code | Mnemonic | Effect | Flags |
|------|----------|--------|-------|
| D0 | `OUT`  | out port = A, and toggle the OUT_STROBE pin | – |
| D1 | `IN`   | A = in port | Z |
| D2 | `SHL`  | C ← A ← 0 | Z, C |
| D3 | `SHR`  | 0 → A → C | Z, C |
| D4 | `NOT`  | A = ~A | Z |
| D5 | `HLT`  | stop (HALTED pin high) until reset | – |
| D6 | `TAX`  | X = A | Z |
| D7 | `TXA`  | A = X | Z |
| D8 | `INX`  | X = X + 1 | Z |
| D9 | `DEX`  | X = X − 1 | Z |
| DA | `PUSH` | SP −= 1, M[SP] = A | – |
| DB | `POP`  | A = M[SP], SP += 1 | Z |
| DC | `RET`  | PC = {M[SP], M[SP+1]}, SP += 2 | – |
| DD–DF | `NOP` | nothing | – |

Flags that an instruction doesn't list are kept. In particular `LDA` and `ORI` keep `C`, which the shift-and-add examples rely on.

AND, OR and XOR exist only as immediate forms. In version 1 they also had memory forms, which gave up their opcodes to the X and stack instructions.

## Input and output

- `OUT` writes A to the output pins and toggles OUT_STROBE (`uio[5]`), so a listener can capture every byte, repeated values included.
- `IN` reads the 8 input pins.
- Text output (`lib/print.asm`) is built on `OUT`.
- Keyboard input (`lib/input.asm`) uses a mailbox of two RAM bytes, `0xEFE` and `0xEFF`, which the owner of the RAM fills in. It is a software convention, not part of the chip.

[io.md](io.md) describes all of it: the handshake, the libraries, and how to run a program with input and output on each platform.

## Stack

The stack is the top page of memory, `0xF00`–`0xFFF`. A stack address is `0xF00 + SP`. SP starts at 0 and pushes decrement it first, so the first `PUSH` writes `0xFFF` and the first `CALL` writes `0xFFE`/`0xFFF`. Return addresses are stored high byte first.

The stack is 256 bytes: 128 nested CALLs, or fewer when they also push data. Programs must keep code and data below `0xF00`. `cpu8asm` warns when they don't.

## Timing

The memory is 23LC512-style: READ `0x03` / WRITE `0x02` followed by a **16-bit address**. This is the same protocol as the TT demo board's RP2040 [spi-ram-emu](https://github.com/MichaelBell/spi-ram-emu). The CPU sends the top 4 address bits as 0, and SCK runs at clk/2.

| Instruction class | Clocks |
|-------------------|--------|
| no memory access (immediates, jumps, misc) | 88 |
| one data byte (`LDA STA ADD SUB CMP`, `,X` forms, `PUSH POP`) | 160 |
| `CALL`, `RET` (two bytes) | 176 |
| `STRLEN` | 161 + 17 per character |

Every fetch is one 40-bit SPI read: an 8-bit command, a 16-bit address, then 2 data bytes. After every transaction CS stays high for at least 8 clocks, because spi-ram-emu on the RP2040 needs that time (about 50 of its clocks) to re-arm. That's 800 ns at 10 MHz.

MISO is sampled at the end of the SCK high phase, or at the SCK rising edge when the MISO_RISE strap (`uio[6]`) is high. See [info.md](info.md) for which setting suits which RAM. Reset is synchronised internally: the CPU starts fetching 2 clocks after `rst_n` goes high. `cpu8sim` counts clocks with exactly this model. `test/test_programs.py` checks it against the RTL for every example program.

## STRLEN

`STRLEN ea` is a normal multi-cycle instruction, not a co-processor. It sends a single SPI READ for `ea` and keeps CS low. The RAM streams consecutive bytes in sequential mode, and the CPU counts them into A until a `0x00` byte arrives. If no NUL turns up within 255 bytes, it stops at 255 and sets C.

## Assembler (`tools/cpu8asm`)

```
N = 10                  ; constant (or: .equ N, 10)
loop:   LDA table,X     ; indexed form
        CALL sub
        .org 0x200      ; location counter
table:  .byte 1, 2, N*3 ; also .word (2 bytes, high first), .space n[, fill], .align n
msg:    .asciz "hi\n"   ; .ascii: without the NUL
```

- Numbers can be written as `12`, `0x0C`, `0b1100`, `'a'` or `'\n'`.
- Expressions support `+ - * / % ( )`, `<x` (low byte), `>x` (x >> 8), and `$` (the current address).
- Mnemonics and labels are case-insensitive.
- An immediate can be written with or without a leading `#`.
- `.include "file"` inserts another source file. The path is relative to the including file.
