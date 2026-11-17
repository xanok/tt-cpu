; Read the input port and print: number of 1 bits, parity, and the byte
; with its bits reversed. Input 0xB5 = 10110101 -> 5, 1, 0xAD = 173.
; input: 0xB5
; expect: 5 1 173

        IN
        STA v
        LDX 8
loop:   LDA v
        SHR             ; C = lowest bit
        STA v
        LDA rev         ; rev = rev << 1 | C  (LDA keeps C)
        JC  one
        SHL
        STA rev
        JMP next
one:    SHL
        ORI 1
        STA rev
        LDA cnt
        ADI 1
        STA cnt
next:   DEX
        JNZ loop
        LDA cnt
        OUT
        ANI 1           ; parity
        OUT
        LDA rev
        OUT
        HLT

v:      .byte 0
rev:    .byte 0
cnt:    .byte 0
