; Fill a 32-byte block (src[i] = 7*i + 3), copy it with LDA/STA ea,X,
; compare the copy and print 1 if equal, then the 8-bit sum of the copy.
; expect: 1 240

N = 32
        LDX 0
        LDI 3
fill:   STA src,X
        ADI 7
        INX
        CPX N
        JNZ fill

        LDX 0
copy:   LDA src,X
        STA dst,X
        INX
        CPX N
        JNZ copy

        LDX 0
cmp:    LDA dst,X
        STA t
        LDA src,X
        CMP t
        JNZ bad
        INX
        CPX N
        JNZ cmp
        LDI 1
        OUT

        LDX 0
        LDI 0
sum:    ADD dst,X
        INX
        CPX N
        JNZ sum
        OUT
        HLT
bad:    LDI 0
        OUT
        HLT

t:      .byte 0
        .org 0x400
src:    .space N
        .org 0x7F0      ; dst crosses a 256-byte page
dst:    .space N
