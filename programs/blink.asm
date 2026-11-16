; Binary counter on the Tang Nano LEDs (about 2 steps per second at 27 MHz).
; Each instruction takes ~100-180 clocks because every byte comes over SPI.

        LDI 0
        STA count
main:   LDA count
        OUT
        ADI 1
        STA count
        LDI 0
        STA outer
outer_l:
        LDI 0
inner:  ADI 1           ; 256 iterations, A wraps to 0
        JNZ inner
        LDA outer
        ADI 1
        STA outer
        JNZ outer_l     ; 256 iterations
        JMP main

count:  .byte 0
outer:  .byte 0
