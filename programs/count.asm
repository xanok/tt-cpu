; Count 0..9 on the output port.
; expect: 0 1 2 3 4 5 6 7 8 9

        LDI 0
loop:   OUT
        ADI 1
        CPI 10          ; Z when A == 10
        JNZ loop
        HLT
