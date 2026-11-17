; Towers of Hanoi with 4 disks from A to C, recursive.
; The arguments are globals; each level saves them on the stack.
; expect-text: AB\nAC\nBC\nAB\nCA\nCB\nAB\nAC\nBC\nBA\nCA\nBC\nAB\nAC\nBC\n

        LDI 4
        STA n
        LDI 'A'
        STA fr
        LDI 'C'
        STA to
        LDI 'B'
        STA vi
        CALL hanoi
        HLT

; move n disks from fr to to, using vi
hanoi:  LDA n
        JZ  hret
        PUSH            ; save n, fr, to, vi
        LDA fr
        PUSH
        LDA to
        PUSH
        LDA vi
        PUSH
        LDA n           ; hanoi(n-1, fr, vi, to)
        SBI 1
        STA n
        LDA to
        STA t
        LDA vi
        STA to
        LDA t
        STA vi
        CALL hanoi
        POP             ; restore in reverse order
        STA vi
        POP
        STA to
        POP
        STA fr
        POP
        STA n
        LDA fr          ; print the move "fr to"
        OUT
        LDA to
        OUT
        LDI '\n'
        OUT
        LDA n           ; hanoi(n-1, vi, to, fr) as a tail call
        SBI 1
        STA n
        LDA fr
        STA t
        LDA vi
        STA fr
        LDA t
        STA vi
        JMP hanoi
hret:   RET

n:      .byte 0
fr:     .byte 0
to:     .byte 0
vi:     .byte 0
t:      .byte 0
