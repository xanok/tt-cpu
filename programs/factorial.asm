; Recursive factorial: fact(n) = n * fact(n-1), n saved on the stack.
; Prints 0! .. 5!
; expect: 1 1 2 6 24 120

        LDI 0
        STA n
loop:   LDA n
        CALL fact
        OUT
        LDA n
        ADI 1
        STA n
        CPI 6
        JNZ loop
        HLT

; A = A!   (A <= 5, the result must fit in 8 bits)
fact:   CPI 0
        JZ  one
        PUSH            ; save n
        SBI 1
        CALL fact       ; A = (n-1)!
        STA f
        POP             ; A = n
        TAX
        LDI 0
mul:    ADD f           ; A = f * n by repeated addition
        DEX
        JNZ mul
        RET
one:    LDI 1
        RET

n:      .byte 0
f:      .byte 0
