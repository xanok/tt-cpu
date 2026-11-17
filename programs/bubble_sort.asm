; Bubble sort of 10 bytes in memory, then print them.
; expect: 3 8 11 12 22 25 45 64 77 90

N = 10
outer:  LDI 0
        STA swapped
        LDX 0
inner:  LDA data+1,X
        STA t
        LDA data,X
        CMP t           ; C: data[x] < data[x+1], Z: equal
        JC  noswap
        JZ  noswap
        STA data+1,X    ; swap
        LDA t
        STA data,X
        LDI 1
        STA swapped
noswap: INX
        CPX N-1
        JNZ inner
        LDA swapped
        JNZ outer

        LDX 0
print:  LDA data,X
        OUT
        INX
        CPX N
        JNZ print
        HLT

data:   .byte 64, 25, 12, 22, 11, 90, 3, 77, 45, 8
t:      .byte 0
swapped: .byte 0
