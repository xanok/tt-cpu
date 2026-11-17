; 8-bit Galois LFSR pseudo-random numbers (taps 0xB8, period 255).
; expect: 184 92 46 23 179 225 200 100 50 25 180 90

        LDI 1           ; seed
        LDX 12
loop:   SHR             ; C = bit shifted out
        JC  tap
        JMP show
tap:    XRI 0xB8
show:   OUT
        DEX
        JNZ loop
        HLT
