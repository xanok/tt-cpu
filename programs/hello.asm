; Hello world: print a NUL-terminated string with an indexed loop.
; expect-text: Hello, Tiny Tapeout!\n

        LDX 0
loop:   LDA msg,X       ; A = msg[X], Z set at the terminating NUL
        JZ  done
        OUT
        INX
        JMP loop
done:   HLT

msg:    .asciz "Hello, Tiny Tapeout!\n"
