; 8 x 8 -> 16 bit shift-and-add multiply subroutine.
; 123*45 = 5535 (0x159F), 255*255 = 65025 (0xFE01), 12*10 = 120.
; Each product is printed as high byte, low byte.
; expect: 21 159 254 1 0 120

        LDI 123
        STA m1
        LDI 45
        STA m2
        CALL mul8
        CALL show
        LDI 255
        STA m1
        STA m2
        CALL mul8
        CALL show
        LDI 12
        STA m1
        LDI 10
        STA m2
        CALL mul8
        CALL show
        HLT

show:   LDA p_hi
        OUT
        LDA p_lo
        OUT
        RET

; p_hi:p_lo = m1 * m2   (m2 is destroyed, X used as bit counter)
mul8:   LDI 0
        STA p_lo
        STA p_hi
        STA mc_hi
        LDA m1
        STA mc_lo       ; multiplicand, 16 bits, shifted left each step
        LDX 8
mloop:  LDA m2
        SHR             ; C = next multiplier bit
        STA m2
        JC  addit
        JMP shift
addit:  LDA p_lo        ; p += mc
        ADD mc_lo
        STA p_lo
        JC  carry1
        JMP addhi
carry1: LDA p_hi
        ADI 1
        STA p_hi
addhi:  LDA p_hi
        ADD mc_hi
        STA p_hi
shift:  LDA mc_lo       ; mc <<= 1
        SHL
        STA mc_lo
        LDA mc_hi       ; LDA keeps C
        JC  sh1
        SHL
        JMP sh2
sh1:    SHL
        ORI 1
sh2:    STA mc_hi
        DEX
        JNZ mloop
        RET

m1:     .byte 0
m2:     .byte 0
mc_lo:  .byte 0
mc_hi:  .byte 0
p_lo:   .byte 0
p_hi:   .byte 0
