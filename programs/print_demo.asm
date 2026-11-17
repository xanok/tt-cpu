; Text output with the print library (lib/print.asm): strings, decimal and
; hex numbers. On the board: cpu8load -a print_demo.bin
; expect-text: cpu8 says hi!\n7 x 6 = 42\n0xC8 = 200\nrepeats: aaa\n

        LDI >greet
        LDX <greet
        CALL print
        LDI 7
        CALL print_dec
        LDI >times
        LDX <times
        CALL print
        LDI 6
        CALL print_dec
        LDI >equals
        LDX <equals
        CALL print
        LDI 42
        CALL print_dec
        CALL newline
        LDI '0'
        OUT
        LDI 'x'
        OUT
        LDI 200
        CALL print_hex
        LDI >equals
        LDX <equals
        CALL print
        LDI 200
        CALL print_dec
        CALL newline
        LDI >rep
        LDX <rep
        CALL print          ; the same byte three times: the strobe still shows each one
        HLT

        .include "lib/print.asm"

greet:  .asciz "cpu8 says hi!\n"
times:  .asciz " x "
equals: .asciz " = "
        .org 0x9F0          ; a string in another 256-byte page
rep:    .asciz "repeats: aaa\n"
