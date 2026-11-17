// Random-program test: runs random cpu8 programs on the Verilog (Verilator)
// and on the C instruction model (tools/cpu8core.h, the core of cpu8sim) and
// compares them after EVERY instruction: A, X, SP, PC, Z, C, the output
// port and strobe; the whole memory at the end; and the clock count.
// It also resets the CPU at random moments.
//
//   fuzz [-n programs] [-s seed] [-m mode] [-v]      random programs
//   fuzz -p prog.bin [-i ui_in]                      one program from a file
//
// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Vfuzz_top.h"
#include "Vfuzz_top___024root.h"
#include "verilated.h"
#include "verilated_cov.h"

extern "C" {
#include "cpu8core.h"
}

#define CORE(sig) (top->rootp->fuzz_top__DOT__dut__DOT__core__DOT__##sig)
#define RAM       (top->rootp->fuzz_top__DOT__ram__DOT__mem)

enum { S_FETCH = 0, S_HALT = 4 };

static Vfuzz_top *top;
static uint64_t rng_state = 1;
static int verbose;

static uint32_t rnd(void)  // xorshift64*
{
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return (uint32_t)((rng_state * 0x2545F4914F6CDD1DULL) >> 32);
}
static unsigned rnd_n(unsigned n) { return rnd() % n; }

static void tick(void)
{
    top->clk = 0;
    top->eval();
    top->clk = 1;
    top->eval();
}

// ------------------------------------------------------------ programs

// An address that is likely to be interesting.
static unsigned pick_addr(void)
{
    switch (rnd_n(10)) {
    case 0: return 0xEF0 + rnd_n(0x110);        // mailbox bytes, stack page
    case 1: return 0xFF8 + rnd_n(8);            // top of memory
    case 2: return rnd_n(16);                   // start of memory / code
    case 3: return rnd_n(0x300);                // code (self-modifying)
    case 4: return 0xC00 + rnd_n(0x160);        // long string area
    case 5: case 6: return 0x800 + rnd_n(0x100);
    default: return rnd_n(0x1000);
    }
}

static void put_instr(unsigned char *m, unsigned at, unsigned b0, unsigned lo)
{
    m[at & 0xFFF] = (unsigned char)b0;
    m[(at + 1) & 0xFFF] = (unsigned char)lo;
}

// mode 0: every byte random. mode 1: weighted valid instructions in
// 0x000-0x2FF over random data. mode 2: mode 1 with some random bytes mixed in.
static void make_program(unsigned char *m, int mode)
{
    for (int i = 0; i < 4096; i++)
        m[i] = (unsigned char)rnd();
    if (mode == 0)
        return;
    // long runs of non-zero bytes so STRLEN can saturate, short strings elsewhere
    for (int i = 0xC00; i < 0xD60; i++)
        m[i] = (unsigned char)(1 + rnd_n(255));
    for (int i = 0x800; i < 0x900; i++)
        if (rnd_n(4) == 0)
            m[i] = 0;
    unsigned len = 0x40 + rnd_n(0x2C0);
    for (unsigned at = 0; at < len; at += 2) {
        unsigned r = rnd_n(100), ea = pick_addr(), imm = rnd_n(4) ? rnd() & 0xFF : (rnd_n(2) ? 0 : 0xFF);
        static const unsigned mem_ops[] = {1, 2, 3, 4, 5, 6, 7, 0xE};
        if (r < 22) {                                   // immediate group
            put_instr(m, at, rnd_n(10) ? rnd_n(11) : rnd_n(16), imm);
        } else if (r < 52) {                            // memory access
            put_instr(m, at, mem_ops[rnd_n(8)] << 4 | ea >> 8, ea);
        } else if (r < 60) {                            // CALL forward / anywhere in code
            unsigned t = rnd_n(4) ? (at + 2 + 2 * rnd_n(24)) : 2 * rnd_n(len / 2);
            put_instr(m, at, 0x80 | (t >> 8 & 0xF), t);
        } else if (r < 72) {                            // jumps, mostly short and forward
            unsigned t = rnd_n(6) ? (at + 2 + 2 * rnd_n(12)) : 2 * rnd_n(len / 2);
            static const unsigned j[] = {9, 0xA, 0xB, 0xC};
            put_instr(m, at, j[rnd_n(4)] << 4 | (t >> 8 & 0xF), t);
        } else if (r < 95) {                            // misc group
            unsigned h;
            do
                h = rnd_n(16);
            while (h == 5 && rnd_n(8));                 // HLT only sometimes
            put_instr(m, at, 0xD0 | h, rnd());
        } else {                                        // STRLEN
            put_instr(m, at, 0xF0 | ea >> 8, ea);
        }
        if (mode == 2 && rnd_n(10) == 0)
            m[at + rnd_n(2)] = (unsigned char)rnd();
    }
    put_instr(m, len, 0xD5, 0);                         // HLT
}

// ------------------------------------------------------------ one run

struct result { int ok; uint64_t instrs, clocks, resets; int halted; };

static int fail(const char *what, const struct cpu *s, unsigned long long n)
{
    fprintf(stderr, "MISMATCH %s after %llu instructions\n", what, n);
    fprintf(stderr, "  model: PC=%03X A=%02X X=%02X SP=%02X Z=%d C=%d OUT=%02X\n",
            s->pc, s->a, s->x, s->sp, s->z, s->c, s->out);
    fprintf(stderr, "  RTL:   PC=%03X A=%02X X=%02X SP=%02X Z=%d C=%d OUT=%02X state=%d\n",
            CORE(pc), CORE(a), CORE(x), CORE(sp), CORE(zf), CORE(cf), top->uo_out, CORE(state));
    return 0;
}

static struct cpu model;  // static: 4 KB

static struct result run(const unsigned char *image, unsigned ui, unsigned max_instr, int with_resets)
{
    struct result res = {1, 0, 0, 0, 0};
    memset(&model, 0, sizeof model);
    memcpy(model.mem, image, 4096);
    model.in = (unsigned char)ui;
    for (int i = 0; i < 4096; i++)
        RAM[i] = image[i];

    top->ui_in = ui;
    top->rst_n = 0;
    for (int i = 0; i < 6; i++)
        tick();
    top->rst_n = 1;

    uint64_t cyc = 0;       // clocks since reset was released
    unsigned outs = 0;      // OUT instructions since reset (strobe parity)
    unsigned prev = S_FETCH;
    uint64_t guard = 0;
    for (;;) {
        tick();
        cyc++;
        res.clocks++;
        unsigned st = CORE(state);
        int boundary = (st == S_FETCH && prev != S_FETCH) || (st == S_HALT && prev != S_HALT);
        prev = st;
        if (++guard > 6000) {  // longest instruction: STRLEN, 73 + 255 * 17 clocks
            res.ok = fail("RTL stuck (no instruction boundary)", &model, model.instrs);
            return res;
        }
        if (!boundary)
            continue;
        guard = 0;

        int out = step(&model);
        if (out >= 0)
            outs++;
        res.instrs++;
        if (verbose)
            fprintf(stderr, "%6llu  PC=%03X A=%02X X=%02X SP=%02X Z=%d C=%d\n",
                    (unsigned long long)res.instrs, model.pc, model.a, model.x, model.sp, model.z, model.c);
        if (CORE(pc) != model.pc || CORE(a) != model.a || CORE(x) != model.x ||
            CORE(sp) != model.sp || CORE(zf) != model.z || CORE(cf) != model.c) {
            res.ok = fail("registers", &model, res.instrs);
            return res;
        }
        if (top->uo_out != model.out || top->strobe != (outs & 1)) {
            res.ok = fail("output port / strobe", &model, res.instrs);
            return res;
        }
        if ((st == S_HALT) != (model.halted != 0)) {
            res.ok = fail("halt state", &model, res.instrs);
            return res;
        }
        // The model counts the 8-clock CS gap with the instruction before it;
        // the RTL reaches the boundary a few clocks earlier. A HLT is
        // always 2 clocks ahead of the model. Any other difference is a timing bug.
        long long off = (long long)model.clocks - (long long)cyc;
        if (model.halted ? off != -2 : (off < 2 || off > 9)) {
            fprintf(stderr, "clock count: model %llu, RTL %llu (difference %lld)\n",
                    model.clocks, (unsigned long long)cyc, off);
            res.ok = fail("clock count", &model, res.instrs);
            return res;
        }
        if (model.halted || res.instrs >= max_instr)
            break;

        // Random reset. Only while the next instruction is being fetched
        // (the first ~70 clocks: reads only), so no write is cut in half
        // and the memory stays comparable.
        if (with_resets && rnd_n(400) == 0) {
            unsigned wait = rnd_n(68), hold = 1 + rnd_n(6);
            for (unsigned i = 0; i < wait; i++)
                tick();
            top->rst_n = 0;
            for (unsigned i = 0; i < hold; i++)
                tick();
            top->rst_n = 1;
            // As after power-on: the clock count starts when rst_n goes high;
            // the synchroniser holds the CPU in reset for 2 more clocks.
            tick();
            tick();
            cyc = 2;
            model.pc = 0;
            model.a = model.x = model.sp = model.z = model.c = model.out = 0;
            model.clocks = 0;
            outs = 0;
            prev = S_FETCH;
            res.clocks += wait + hold + 2;
            res.resets++;
            if (CORE(state) != S_FETCH || CORE(pc) != 0 || CORE(a) != 0 || CORE(x) != 0 ||
                CORE(sp) != 0 || CORE(zf) != 0 || CORE(cf) != 0 || top->uo_out != 0 ||
                top->strobe != 0) {
                res.ok = fail("state after reset", &model, res.instrs);
                return res;
            }
        }
    }
    res.halted = model.halted;
    for (int i = 0; i < 4096; i++)
        if (RAM[i] != model.mem[i]) {
            fprintf(stderr, "memory[0x%03X]: model %02X, RTL %02X\n", i, model.mem[i], RAM[i]);
            res.ok = fail("memory", &model, res.instrs);
            return res;
        }
    return res;
}

int main(int argc, char **argv)
{
    Verilated::commandArgs(argc, argv);
    long nprog = 1000, mode = -1, ui = -1;
    unsigned long long seed = 1;
    const char *file = NULL, *cov = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc) nprog = atol(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) mode = atol(argv[++i]);
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) file = argv[++i];
        else if (!strcmp(argv[i], "-i") && i + 1 < argc) ui = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) cov = argv[++i];
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else {
            fprintf(stderr, "usage: fuzz [-n programs] [-s seed] [-m 0|1|2] [-c coverage.dat] [-v]\n"
                            "       fuzz -p prog.bin [-i ui_in] [-c coverage.dat] [-v]\n");
            return 2;
        }
    }
    top = new Vfuzz_top;
    static unsigned char image[4096];
    int rc = 0;

    if (file) {
        FILE *f = fopen(file, "rb");
        if (!f) { perror(file); return 2; }
        memset(image, 0, sizeof image);
        if (fread(image, 1, sizeof image, f) == 0) { fprintf(stderr, "%s: empty\n", file); return 2; }
        fclose(f);
        struct result r = run(image, ui < 0 ? 0 : (unsigned)ui, 20000000, 0);
        printf("%s: %s, %llu instructions, %llu clocks%s\n", file, r.ok ? "ok" : "MISMATCH",
               (unsigned long long)r.instrs, (unsigned long long)r.clocks, r.halted ? ", halted" : "");
        rc = !r.ok;
    } else {
        uint64_t instrs = 0, clocks = 0, resets = 0, halts = 0;
        for (long p = 0; p < nprog; p++) {
            rng_state = seed * 0x9E3779B97F4A7C15ULL + (uint64_t)p * 0xD1B54A32D192ED03ULL + 1;
            int m = mode >= 0 ? (int)mode : (int)(p % 3);
            make_program(image, m);
            unsigned in = ui >= 0 ? (unsigned)ui : rnd() & 0xFF;
            struct result r = run(image, in, m == 0 ? 400 : 3000, 1);
            instrs += r.instrs; clocks += r.clocks; resets += r.resets; halts += r.halted;
            if (!r.ok) {
                char name[64];
                snprintf(name, sizeof name, "fail_s%llu_p%ld.bin", seed, p);
                FILE *f = fopen(name, "wb");
                if (f) { fwrite(image, 1, sizeof image, f); fclose(f); }
                fprintf(stderr, "program %ld (seed %llu, mode %d, ui_in 0x%02X) saved as %s\n",
                        p, seed, m, in, name);
                rc = 1;
                break;
            }
        }
        printf("%s: %ld programs, %llu instructions, %llu clocks, %llu halted, %llu resets\n",
               rc ? "FAILED" : "ok", nprog, (unsigned long long)instrs, (unsigned long long)clocks,
               (unsigned long long)halts, (unsigned long long)resets);
    }
    top->final();
    if (cov)
        VerilatedCov::write(cov);
    delete top;
    return rc;
}
