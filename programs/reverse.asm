; Reverse a string with the stack: PUSH every character, then POP them.
; expect-text: desserts\n

        LDX 0
push:   LDA word,X
        JZ  popall
        PUSH
        INX
        JMP push
popall: POP
        OUT
        DEX
        JNZ popall
        LDI '\n'
        OUT
        HLT

word:   .asciz "stressed"
