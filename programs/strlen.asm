; Hardware STRLEN against a software loop: both print 12, then 0 for "".
; Run with "cpu8sim -s" to compare the clock counts of the two halves.
; expect: 12 12 0 0

        STRLEN s1       ; hardware: one SPI read, streams until NUL
        OUT
        LDX 0           ; software: count with LDA s1,X
sw:     LDA s1,X
        JZ  swdone
        INX
        JMP sw
swdone: TXA
        OUT
        STRLEN s2
        OUT
        LDX 0
sw2:    LDA s2,X
        JZ  sw2done
        INX
        JMP sw2
sw2done: TXA
        OUT
        HLT

s1:     .asciz "Tiny Tapeout"
s2:     .asciz ""
