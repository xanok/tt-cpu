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
|   | `NOP` (DF, D5–DE) | nothing | – |
| E | `CMP ea`  | flags of A − M[ea], A unchanged | Z, C |
| F | `HLT`     | stop (HALTED pin high) until reset | – |

## Timing

SCK runs at clk/2. Every fetch is one 48-bit SPI read (96 clocks). Memory operands and `STA` add another 40-bit transaction (80 clocks). That works out to about 100 clocks for most instructions and about 180 for memory instructions.
