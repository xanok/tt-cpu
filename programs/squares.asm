; Print the squares 1..225 as decimal text, using a print_dec subroutine
; that itself calls a subroutine (nested CALLs).
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

; print A in decimal, without leading zeros
print_dec:
        STA pv
        LDI 0
        STA lead
        LDX 0           ; hundreds
h:      LDA pv
        CPI 100
        JC  hdone
        SBI 100
        STA pv
        INX
        JMP h
hdone:  CALL digit
        LDX 0           ; tens
t:      LDA pv
        CPI 10
        JC  tdone
        SBI 10
        STA pv
        INX
        JMP t
tdone:  CALL digit
        LDA pv          ; ones: always printed
        ADI '0'
        OUT
        RET

; print digit X, unless it is a leading zero
digit:  TXA
        JNZ dprint
        LDA lead
        JZ  dskip
dprint: TXA
        ADI '0'
        OUT
        LDI 1
        STA lead
dskip:  RET

sq:     .byte 0
odd:    .byte 0
pv:     .byte 0
lead:   .byte 0
