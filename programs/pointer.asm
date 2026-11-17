; Pointers with the 12-bit X register: X holds a full address, so
; "LDA 0,X" reads the byte X points to.
; 1. Walk a table of string addresses and print each string.
; 2. Add up a 300-byte block (more than one page) counting X down.
;    300 * 3 = 900 = 0x384: prints the low byte of the sum in hex.
; expect-text: one\ntwo\nthree\n84\n

N = 300
        LDI 0
        STA idx
next:   LDX 0
        LDA idx
        TAX
        LDA table,X         ; high byte of the string address
        STA hi
        LDA table+1,X       ; low byte
        TAX
        LDA hi
        TAXH                ; X = address of the string
        TXHA
        JNZ show
        TXA
        JZ  sum             ; address 0 ends the table
show:   LDA 0,X
        JZ  eol
        OUT
        INX
        JMP show
eol:    CALL newline
        LDA idx
        ADI 2
        STA idx
        JMP next

sum:    LDX <N
        LDXH >N             ; X = 300
loop:   LDA total
        ADD block-1,X       ; block[X-1]
        STA total
        DEX                 ; Z when all 12 bits are 0
        JNZ loop
        LDA total
        CALL print_hex
        CALL newline
        HLT

        .include "lib/print.asm"

idx:    .byte 0
hi:     .byte 0
total:  .byte 0
table:  .word s1, s2, s3, 0
s1:     .asciz "one"
        .org 0x234
s2:     .asciz "two"
        .org 0x5F0
s3:     .asciz "three"
        .org 0x700
block:  .space N, 3
