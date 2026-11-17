; Text input library for cpu8: the input mailbox.
;
; The chip has no input instruction for text (IN only reads the 8 input
; pins), but the RP2040 on the demo board is the CPU's RAM, so it can put
; bytes into memory while the program runs. Two bytes just below the stack
; page are used:
;
;   0xEFE  flag   0 = empty, not 0 = a byte is waiting in 0xEFF
;   0xEFF  data   the input byte
;
; The RP2040 (or the simulator) waits until the flag is 0, writes the next
; byte to 0xEFF and sets the flag. The program reads the byte and writes 0
; to the flag. Run with "cpu8load -k" (keyboard) or "cpu8sim -k" / "-I TEXT".
;
;   CALL getchar     ; wait for one byte, return it in A (X is kept)
;
;   LDI >buf         ; buffer address: A = high byte
;   LDX <buf         ;                 X = low byte
;   CALL getline     ; read up to the newline, store it NUL-terminated
;                    ; (without the newline); returns the length in A
;
; getline stores at most 31 characters, so the buffer needs 32 bytes; the
; rest of a longer line is dropped. Carriage returns are ignored.
; Labels start with "in_". .include this file after your program's last
; instruction (it is code).

MBOX_FLAG = 0xEFE
MBOX_DATA = 0xEFF

; wait for an input byte, return it in A
getchar:
        LDA MBOX_FLAG
        JZ  getchar
        LDA MBOX_DATA
        PUSH
        LDI 0
        STA MBOX_FLAG       ; mailbox is free again
        POP
        RET

; read a line into the buffer at address {A, X}; A = length
getline:
        ORI 0x60            ; opcode of "STA ea,X" with the high address nibble
        STA in_st
        TXA
        STA in_st+1         ; low address byte
        LDX 0
in_next:
        CALL getchar
        CPI '\r'
        JZ  in_next
        CPI '\n'
        JNZ in_chr
        LDI 0               ; end of line: store the NUL
        JMP in_st
in_chr: CPX 31
        JZ  in_next         ; buffer full: drop the character
in_st:  STA 0,X             ; patched above
        ORI 0
        JZ  in_end          ; that was the NUL
        INX
        JMP in_next
in_end: TXA
        RET
