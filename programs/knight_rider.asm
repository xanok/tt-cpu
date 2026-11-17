; LED scanner for the Tang Nano's 6 LEDs: one light bouncing left and right.
; Endless; the delay subroutine keeps A on the stack.
; limit: 2000000
; expect: 1 2 4 8 16 32 16 8 4 2 1 2 4

        LDI 1
left:   OUT
        CALL delay
        SHL
        CPI 32
        JNZ left
right:  OUT
        CALL delay
        SHR
        CPI 1
        JNZ right
        JMP left

; about 131k instructions (~0.4 s at 27 MHz)
delay:  PUSH
        LDI 0
        STA dcnt
douter: LDX 0
dinner: DEX             ; 256 iterations
        JNZ dinner
        LDA dcnt
        SBI 1
        STA dcnt
        JNZ douter      ; 256 iterations
        POP
        RET

dcnt:   .byte 0
