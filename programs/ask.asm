; ask - interactive input and output: asks two questions, then answers
; Run on the board:      cpu8load -k build/ask.bin
; Run in the simulator:  cpu8sim -a -k build/ask.bin
; (the typed text is echoed by your terminal, not by the program)
; stdin: Ana\n42\n
; expect-text: What is your name? What is your age? Hello, Ana! You are 42 years old.\n

        LDI >q_name
        LDX <q_name
        CALL print
        LDI >name
        LDX <name
        CALL getline

        LDI >q_age
        LDX <q_age
        CALL print
        LDI >age
        LDX <age
        CALL getline

        LDI >m_hello
        LDX <m_hello
        CALL print
        LDI >name
        LDX <name
        CALL print
        LDI >m_you
        LDX <m_you
        CALL print
        LDI >age
        LDX <age
        CALL print
        LDI >m_years
        LDX <m_years
        CALL print
        HLT

        .include "lib/print.asm"
        .include "lib/input.asm"

q_name:  .asciz "What is your name? "
q_age:   .asciz "What is your age? "
m_hello: .asciz "Hello, "
m_you:   .asciz "! You are "
m_years: .asciz " years old.\n"
name:    .space 32
age:     .space 32
