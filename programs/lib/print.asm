; Text output library for cpu8. Every byte written with OUT toggles the
; OUT_STROBE pin (uio[5]); the RP2040 firmware captures it and
; "cpu8load -a" prints it as text (like "cpu8sim -a").
;
;   LDI >msg         ; string address: A = high byte
;   LDX <msg         ;                 X = low byte
;   CALL print       ; NUL-terminated, any length
;
;   LDA value
;   CALL print_dec   ; A as decimal, no leading zeros (0..255)
;   CALL print_hex   ; A as two hex digits
;   CALL newline
;
; All routines change A, X and the flags. Labels start with "pr_".
; .include this file after your program's last instruction (it is code).

; print the NUL-terminated string at address {A, X}
print:  TAXH                ; X = the 12-bit address
pr_ld:  LDA 0,X
        JZ  pr_end
        OUT
        INX
        JMP pr_ld
pr_end: RET

newline:
        LDI '\n'
        OUT
        RET

; print A in decimal
print_dec:
        STA pr_v
        LDI 0
        STA pr_lead         ; set once a digit has been printed
        LDX 0               ; hundreds
pr_h:   LDA pr_v
        CPI 100
        JC  pr_hd
        SBI 100
        STA pr_v
        INX
        JMP pr_h
pr_hd:  CALL pr_digit
        LDX 0               ; tens
pr_t:   LDA pr_v
        CPI 10
        JC  pr_td
        SBI 10
        STA pr_v
        INX
        JMP pr_t
pr_td:  CALL pr_digit
        LDA pr_v            ; ones digit is always printed
        ADI '0'
        OUT
        RET

; print digit X unless it is a leading zero
pr_digit:
        TXA
        JNZ pr_dp
        LDA pr_lead
        JZ  pr_ds
pr_dp:  TXA
        ADI '0'
        OUT
        LDI 1
        STA pr_lead
pr_ds:  RET

; print A as two hex digits
print_hex:
        STA pr_v
        SHR
        SHR
        SHR
        SHR
        CALL pr_nib
        LDA pr_v
        ANI 0x0F
pr_nib: CPI 10              ; A = 0..15 -> '0'..'9', 'A'..'F'
        JC  pr_dec
        ADI 'A' - 10
        OUT
        RET
pr_dec: ADI '0'
        OUT
        RET

pr_v:    .byte 0
pr_lead: .byte 0
