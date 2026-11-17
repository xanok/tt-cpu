; Collatz sequence from 7 (n/2 if even, 3n+1 if odd) until 1,
; followed by the number of steps.
; expect: 7 22 11 34 17 52 26 13 40 20 10 5 16 8 4 2 1 16

        LDI 7
        STA v
        OUT
loop:   LDA v
        CPI 1
        JZ  done
        ANI 1
        JNZ odd
        LDA v           ; even: n / 2
        SHR
        JMP store
odd:    LDA v           ; odd: 3n + 1
        ADD v
        ADD v
        ADI 1
store:  STA v
        OUT
        LDA steps
        ADI 1
        STA steps
        JMP loop
done:   LDA steps
        OUT
        HLT

v:      .byte 0
steps:  .byte 0
