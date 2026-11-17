; Greatest common divisor (Euclid, by subtraction) as a subroutine.
; gcd(252,105)=21  gcd(48,180)=12  gcd(17,5)=1  gcd(200,200)=200
; expect: 21 12 1 200

        LDX 0
next:   LDA pairs,X
        STA ga
        INX
        LDA pairs,X
        STA gb
        INX
        CALL gcd
        OUT
        CPX 8
        JNZ next
        HLT

; A = gcd(ga, gb)
gcd:    LDA ga
        CMP gb
        JZ  gdone
        JC  bbig        ; ga < gb
        SUB gb
        STA ga
        JMP gcd
bbig:   LDA gb
        SUB ga
        STA gb
        JMP gcd
gdone:  RET

pairs:  .byte 252, 105, 48, 180, 17, 5, 200, 200
ga:     .byte 0
gb:     .byte 0
