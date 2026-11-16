# cpu8 instruction set

Registers: `A` (8-bit accumulator), `PC` (12-bit), flags `Z` (zero) and `C` (carry / borrow).
Every instruction is 2 bytes: `[op:4 | hi:4] [lo:8]`. `ea = {hi, lo}` is a 12-bit address, `imm = lo`.
Code and data share one 4 KB address space in the external SPI RAM. Execution starts at `0x000` after reset.

| op | Mnemonic | Effect | Flags |
|----|----------|--------|-------|
| 0 | `LDI imm` | A = imm | Z |
| 1 | `LDA ea`  | A = M[ea] | Z |
| 2 | `STA ea`  | M[ea] = A | – |
| 3 | `ADD ea`  | A = A + M[ea] | Z, C = carry |
| 4 | `SUB ea`  | A = A − M[ea] | Z, C = borrow |
| 5 | `AND ea`  | A = A & M[ea] | Z |
| 6 | `OR ea`   | A = A \| M[ea] | Z |
| 7 | `XOR ea`  | A = A ^ M[ea] | Z |
| 8 | `ADI imm` | A = A + imm (use `ADI -1` to decrement) | Z, C |
| 9 | `JMP ea`  | PC = ea | – |
| A | `JZ ea`   | if Z: PC = ea | – |
| B | `JC ea`   | if C: PC = ea | – |
| C | `JNZ ea`  | if !Z: PC = ea | – |
| D | misc, selected by `hi`: | | |
|   | `OUT` (D0) | out port = A | – |
|   | `IN`  (D1) | A = in port | Z |
|   | `SHL` (D2) | C ← A ← 0 | Z, C |
|   | `SHR` (D3) | 0 → A → C | Z, C |
|   | `NOT` (D4) | A = ~A | Z |
|   | `HLT` (D5) | stop (HALTED pin high) until reset | – |
|   | `NOP` (DF, D6–DE) | nothing | – |
| E | `CMP ea`  | flags of A − M[ea], A unchanged | Z, C |
| F | `STRLEN ea` | A = length of the NUL-terminated string at ea (max 255) | Z = empty, C = no NUL within 255 bytes |

## Timing

SCK runs at clk/2. Every fetch is one 48-bit SPI read (96 clocks). Memory operands and `STA` add another 40-bit transaction (80 clocks). That works out to about 100 clocks for most instructions and about 180 for memory instructions.

## STRLEN

`STRLEN ea` is a normal (multi-cycle) instruction, not a co-processor. It sends a single SPI READ for `ea` and keeps CS low. The RAM streams consecutive bytes in sequential mode, and the CPU counts them into A until a `0x00` byte arrives. It stops at 255 if no NUL turns up, and then sets C.

Cost: about 115 clocks of setup, then about 18 clocks per character (16 SPI clocks plus 2 to check and count). For a 64-character string that is about 1,470 clocks, against about 83,600 for the equivalent software loop (57× faster).

The string address wraps at 4 KB.

```
        STRLEN msg      ; A = 5
        JZ  empty       ; Z set if the string is ""
        ...
msg:    .asciz "hello"
```

## Assembler directives

`.org addr`, `.byte v, ...`, `.ascii "text"`, and `.asciz "text"` (which appends a NUL). Strings accept `\n`-style escapes.
