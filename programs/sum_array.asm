; 16-bit sum of a byte array (ADD ea,X), carry goes into the high byte.
; Prints the high byte, then the low byte: 1020 = 0x03FC.
; expect: 3 252

N = 10
        LDX 0
loop:   LDA sum_lo
        ADD data,X      ; C = carry out of the low byte
        STA sum_lo      ; (STA keeps the flags)
        JC  carry
next:   INX
        CPX N
        JNZ loop
        LDA sum_hi
        OUT
        LDA sum_lo
        OUT
        HLT
carry:  LDA sum_hi
        ADI 1
        STA sum_hi
        JMP next

data:   .byte 200, 150, 100, 50, 25, 255, 128, 64, 32, 16
sum_lo: .byte 0
sum_hi: .byte 0
