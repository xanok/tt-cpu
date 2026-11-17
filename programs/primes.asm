; Sieve of Eratosthenes: print all primes below 100.
; The sieve array starts out as zero (memory is cleared before loading).
; expect: 2 3 5 7 11 13 17 19 23 29 31 37 41 43 47 53 59 61 67 71 73 79 83 89 97

LIMIT = 100
        LDI 2
        STA p
ploop:  TAX
        LDA sieve,X     ; already crossed out?
        JNZ next
        TXA
        OUT             ; p is prime
mark:   TXA             ; X = m, m += p
        ADD p
        TAX
        CPX LIMIT
        JC  setm        ; m < LIMIT
        JMP next
setm:   LDI 1
        STA sieve,X
        JMP mark
next:   LDA p
        ADI 1
        STA p
        CPI LIMIT
        JNZ ploop
        HLT

p:      .byte 0
        .org 0x200
sieve:  .space LIMIT
