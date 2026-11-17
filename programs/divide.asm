; 8-bit restoring division subroutine: quotient and remainder.
; 200/7 = 28 r 4, 255/16 = 15 r 15, 250/200 = 1 r 50, 100/1 = 100 r 0, 7/9 = 0 r 7
; expect: 28 4 15 15 1 50 100 0 0 7

        LDX 0
next:   LDA pairs,X     ; dividend
        STA quo
        INX
        LDA pairs,X     ; divisor
        STA dvs
        INX
        TXA
        PUSH            ; div8 uses X
        CALL div8
        LDA quo
        OUT
        LDA rem
        OUT
        POP
        TAX
        CPX 10
        JNZ next
        HLT

; quo = quo / dvs, rem = quo % dvs
div8:   LDI 0
        STA rem
        LDX 8
dloop:  LDA quo         ; shift quotient left, its top bit into C
        SHL
        STA quo
        LDA rem         ; rem = rem << 1 | C
        JC  d1
        SHL
        JMP d2
d1:     SHL
        ORI 1           ; ORI keeps C (the bit shifted out of rem)
d2:     STA rem
        JC  dsub        ; 9-bit remainder >= 256 > divisor
        CMP dvs
        JC  dnext       ; rem < divisor
dsub:   LDA rem
        SUB dvs
        STA rem
        LDA quo
        ORI 1
        STA quo
dnext:  DEX
        JNZ dloop
        RET

pairs:  .byte 200, 7, 255, 16, 250, 200, 100, 1, 7, 9
quo:    .byte 0
dvs:    .byte 0
rem:    .byte 0
