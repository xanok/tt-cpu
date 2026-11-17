; Fibonacci numbers that fit in 8 bits (stops when the add carries).
; expect: 0 1 1 2 3 5 8 13 21 34 55 89 144 233

        LDA fa
        OUT
loop:   LDA fb
        OUT
        ADD fa          ; next = a + b
        JC  done        ; > 255
        STA t
        LDA fb          ; a = b
        STA fa
        LDA t           ; b = next
        STA fb
        JMP loop
done:   HLT

fa:     .byte 0
fb:     .byte 1
t:      .byte 0
