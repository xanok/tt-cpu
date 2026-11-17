// Records the SPI waveform the chip produces while it runs a program
// (Verilator model of the real Verilog + the ideal RAM model), for replay
// into the emulated RP2040 (replay.mjs).
//
//   trace prog.bin trace.txt final.bin [ui_in]
//
// trace.txt: one line per chip clock, "<cs><sck><mosi><check><expect>":
// the three pins during that clock period, and whether the chip samples a
// READ data bit on MISO at the end of it, with the value it must see.
// final.bin: the 4 KB memory when the program halts.
// SPDX-License-Identifier: Apache-2.0
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Vfuzz_top.h"
#include "Vfuzz_top___024root.h"
#include "verilated.h"

#define ROOT      (top->rootp)
#define CORE(sig) (ROOT->fuzz_top__DOT__dut__DOT__core__DOT__##sig)

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: trace prog.bin trace.txt final.bin [ui_in]\n");
        return 2;
    }
    Vfuzz_top *top = new Vfuzz_top;
    unsigned char image[4096] = {0};
    FILE *f = fopen(argv[1], "rb");
    if (!f || fread(image, 1, sizeof image, f) == 0) {
        perror(argv[1]);
        return 2;
    }
    fclose(f);
    for (int i = 0; i < 4096; i++)
        ROOT->fuzz_top__DOT__ram__DOT__mem[i] = image[i];
    FILE *out = fopen(argv[2], "w");
    if (!out) {
        perror(argv[2]);
        return 2;
    }
#ifdef MISO_RISE
    const int strap = 1;
#else
    const int strap = 0;
#endif
    auto tick = [&]() {
        top->clk = 0; top->eval();
        top->clk = 1; top->eval();
    };
    top->ui_in = argc > 4 ? strtol(argv[4], NULL, 0) : 0;
    top->rst_n = 0;
    for (int i = 0; i < 6; i++)
        tick();
    top->rst_n = 1;

    unsigned long clocks = 0, checks = 0, tail = 0;
    int prev_cs = 1, prev_sck = 0, nrise = 0;
    unsigned cmd = 0;
    while (tail < 20 && clocks < 400000000ul) {
        unsigned uio = ROOT->fuzz_top__DOT__uio_out;
        int cs = uio & 1, mosi = uio >> 1 & 1, sck = uio >> 3 & 1;
        if (cs) {
            nrise = 0;
            cmd = 0;
        } else if (sck && !prev_sck) {
            if (nrise < 8)
                cmd = cmd << 1 | (unsigned)mosi;
            nrise++;
        }
        (void)prev_cs;
        prev_cs = cs;
        prev_sck = sck;
        // Does the chip sample MISO at the clock edge that ends this period?
        int samples = CORE(busy) && (strap ? !sck : sck);
        int check = samples && cmd == 0x03 && CORE(bitcnt) >= 24;
        int expect = ROOT->fuzz_top__DOT__miso;
        fprintf(out, "%d%d%d%d%d\n", cs, sck, mosi, check, check ? expect : 0);
        checks += (unsigned long)check;
        clocks++;
        if (top->halted)
            tail++;
        tick();
    }
    fclose(out);
    f = fopen(argv[3], "wb");
    for (int i = 0; i < 4096; i++)
        fputc(ROOT->fuzz_top__DOT__ram__DOT__mem[i], f);
    fclose(f);
    printf("%s: %lu clocks, %lu MISO bits to check, strap %d%s\n", argv[1], clocks, checks, strap,
           top->halted ? "" : " (NOT HALTED)");
    return top->halted ? 0 : 1;
}
