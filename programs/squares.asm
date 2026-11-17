; Print the squares 1..225 as decimal text with print_dec from
; lib/print.asm (which itself calls a subroutine: nested CALLs).
; expect-text: 1 4 9 16 25 36 49 64 81 100 121 144 169 196 225\n

        LDI 0
        STA sq
        LDI 1
        STA odd
loop:   LDA sq          ; (n+1)^2 = n^2 + (2n+1)
        ADD odd
        STA sq
        CALL print_dec
        LDA odd
        ADI 2
        STA odd
        CPI 31          ; stop after 15^2
        JZ  done
        LDI ' '
        OUT
        JMP loop
done:   LDI '\n'
        OUT
        HLT

        .include "lib/print.asm"

sq:     .byte 0
odd:    .byte 0
