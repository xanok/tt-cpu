; 16-bit add and subtract with ADC / SBC (add with carry, subtract with
; borrow). LDA keeps C, so "LDA hi / ADC 0" brings the carry of the low
; bytes into the high byte without a branch.
;   0x12FF + 0x0101 = 0x1400, then 0x1400 - 0x0001 = 0x13FF.
; Prints high byte, low byte of each result.
; expect: 20 0 19 255

        LDA a_lo
        ADD b_lo        ; C = carry out of the low bytes
        STA r_lo        ; (STA and LDA keep the flags)
        LDA a_hi
        ADC 0           ; A = a_hi + carry
        ADD b_hi
        STA r_hi
        OUT
        LDA r_lo
        OUT

        LDA r_lo
        SUB c_lo        ; C = borrow out of the low bytes
        STA r_lo
        LDA r_hi
        SBC 0           ; A = r_hi - borrow
        SUB c_hi
        STA r_hi
        OUT
        LDA r_lo
        OUT
        HLT

a_lo:   .byte 0xFF
a_hi:   .byte 0x12
b_lo:   .byte 0x01
b_hi:   .byte 0x01
c_lo:   .byte 0x01
c_hi:   .byte 0x00
r_lo:   .byte 0
r_hi:   .byte 0
