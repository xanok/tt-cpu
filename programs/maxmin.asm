; Find the maximum and minimum of an array and their positions.
; expect: 255 7 0 5

N = 12
        LDA data
        STA max
        STA min
        LDX 1
loop:   LDA data,X
        STA t
        CMP max         ; C: t < max, Z: equal
        JC  notmax
        JZ  notmax
        STA max         ; new maximum (cannot also be a new minimum)
        TXA
        STA imax
        JMP next
notmax: LDA t
        CMP min
        JC  newmin      ; t < min
        JMP next
newmin: STA min
        TXA
        STA imin
next:   INX
        CPX N
        JNZ loop
        LDA max
        OUT
        LDA imax
        OUT
        LDA min
        OUT
        LDA imin
        OUT
        HLT

data:   .byte 17, 3, 200, 45, 99, 0, 128, 255, 1, 64, 7, 33
t:      .byte 0
max:    .byte 0
min:    .byte 0
imax:   .byte 0
imin:   .byte 0
