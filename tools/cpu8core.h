/*
 * cpu8 core: executes one instruction. Shared by the simulator (cpu8sim.c)
 * and the mock demo board (rp2040/mock), so both run the same model.
 * The clock count follows the RTL (src/cpu.v) exactly; test/test_programs.py
 * checks that.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef CPU8CORE_H
#define CPU8CORE_H

#include "cpu8isa.h"

/* clock counts, derived from the RTL state machine */
#define CLK_INSTR  88   /* 40-bit SPI fetch (80) + 8 clocks CS high */
#define CLK_MEM1   72   /* 32-bit SPI transaction (64) + 8 clocks CS high */
#define CLK_MEM2   88   /* 40-bit SPI transaction (CALL, RET) + 8 */
#define CLK_STRLEN 73   /* STRLEN raises CS one clock later than the others */
#define CLK_STRCH  17   /* each extra STRLEN byte: 16 SPI clocks + 1 */

struct cpu {
    unsigned char mem[CPU8_MEM_SIZE];
    unsigned pc;          /* 12 bits */
    unsigned char a, x, sp, z, c;
    unsigned char out, in;
    int halted;
    unsigned long long instrs, clocks;
};

static inline unsigned char rd(struct cpu *s, unsigned addr)
{
    return s->mem[addr & 0xFFF];
}

static inline void wr(struct cpu *s, unsigned addr, unsigned char v)
{
    s->mem[addr & 0xFFF] = v;
}

static inline void set_z(struct cpu *s, unsigned char v)
{
    s->z = (v == 0);
}

/* Execute one instruction. Returns the OUT value written, or -1. */
static inline int step(struct cpu *s)
{
    unsigned char b0 = rd(s, s->pc), lo = rd(s, s->pc + 1);
    unsigned op = b0 >> 4, hi = b0 & 0xF, ea = (hi << 8) | lo;
    int out = -1;
    unsigned r;

    s->pc = (s->pc + 2) & 0xFFF;
    s->instrs++;
    s->clocks += CLK_INSTR;

    switch (op) {
    case 0x0: /* immediate group */
        switch (hi) {
        case 0x0: s->a = lo; set_z(s, s->a); break;                         /* LDI */
        case 0x1: r = s->a + lo; s->c = r > 0xFF; s->a = r; set_z(s, s->a); break; /* ADI */
        case 0x2: s->c = s->a < lo; s->a -= lo; set_z(s, s->a); break;      /* SBI */
        case 0x3: s->a &= lo; set_z(s, s->a); break;                        /* ANI */
        case 0x4: s->a |= lo; set_z(s, s->a); break;                        /* ORI */
        case 0x5: s->a ^= lo; set_z(s, s->a); break;                        /* XRI */
        case 0x6: s->c = s->a < lo; set_z(s, (unsigned char)(s->a - lo)); break; /* CPI */
        case 0x7: s->x = lo; set_z(s, s->x); break;                         /* LDX */
        case 0x8: s->c = s->x < lo; set_z(s, (unsigned char)(s->x - lo)); break; /* CPX */
        default: break;                                                     /* reserved */
        }
        break;
    case 0x1: case 0x5: /* LDA ea / LDA ea,X */
        s->a = rd(s, op == 5 ? ea + s->x : ea);
        set_z(s, s->a);
        s->clocks += CLK_MEM1;
        break;
    case 0x2: case 0x6: /* STA ea / STA ea,X */
        wr(s, op == 6 ? ea + s->x : ea, s->a);
        s->clocks += CLK_MEM1;
        break;
    case 0x3: case 0x7: { /* ADD ea / ADD ea,X */
        r = s->a + rd(s, op == 7 ? ea + s->x : ea);
        s->c = r > 0xFF;
        s->a = r;
        set_z(s, s->a);
        s->clocks += CLK_MEM1;
        break;
    }
    case 0x4: case 0xE: { /* SUB / CMP */
        unsigned char m = rd(s, ea);
        s->c = s->a < m;
        if (op == 4) {
            s->a -= m;
            set_z(s, s->a);
        } else {
            set_z(s, (unsigned char)(s->a - m));
        }
        s->clocks += CLK_MEM1;
        break;
    }
    case 0x8: /* CALL: push PC+2 (high byte first), SP -= 2 */
        s->sp -= 2;
        wr(s, CPU8_STACK_PAGE + s->sp, (unsigned char)(s->pc >> 8));
        wr(s, CPU8_STACK_PAGE + s->sp + 1, (unsigned char)s->pc);
        s->pc = ea;
        s->clocks += CLK_MEM2;
        break;
    case 0x9: s->pc = ea; break;                /* JMP */
    case 0xA: if (s->z) s->pc = ea; break;      /* JZ  */
    case 0xB: if (s->c) s->pc = ea; break;      /* JC  */
    case 0xC: if (!s->z) s->pc = ea; break;     /* JNZ */
    case 0xD: /* misc group */
        switch (hi) {
        case 0x0: s->out = s->a; out = s->a; break;                         /* OUT */
        case 0x1: s->a = s->in; set_z(s, s->a); break;                      /* IN  */
        case 0x2: s->c = s->a >> 7; s->a <<= 1; set_z(s, s->a); break;      /* SHL */
        case 0x3: s->c = s->a & 1; s->a >>= 1; set_z(s, s->a); break;       /* SHR */
        case 0x4: s->a = ~s->a; set_z(s, s->a); break;                      /* NOT */
        case 0x5: s->halted = 1;                                            /* HLT */
                  s->clocks -= 5; break;  /* halts before the CS gap ends */
        case 0x6: s->x = s->a; set_z(s, s->x); break;                       /* TAX */
        case 0x7: s->a = s->x; set_z(s, s->a); break;                       /* TXA */
        case 0x8: s->x++; set_z(s, s->x); break;                            /* INX */
        case 0x9: s->x--; set_z(s, s->x); break;                            /* DEX */
        case 0xA: s->sp--; wr(s, CPU8_STACK_PAGE + s->sp, s->a);            /* PUSH */
                  s->clocks += CLK_MEM1; break;
        case 0xB: s->a = rd(s, CPU8_STACK_PAGE + s->sp); s->sp++;           /* POP */
                  set_z(s, s->a); s->clocks += CLK_MEM1; break;
        case 0xC: s->pc = ((rd(s, CPU8_STACK_PAGE + s->sp) & 0xF) << 8) |   /* RET */
                          rd(s, CPU8_STACK_PAGE + s->sp + 1);
                  s->sp += 2; s->clocks += CLK_MEM2; break;
        default: break;                                                     /* NOP */
        }
        break;
    case 0xF: { /* STRLEN: count bytes until NUL, max 255 */
        unsigned n = 0;
        s->clocks += CLK_STRLEN;
        while (n < 255 && rd(s, ea + n) != 0) {
            n++;
            s->clocks += CLK_STRCH;
        }
        s->c = rd(s, ea + n) != 0;
        s->a = n;
        set_z(s, s->a);
        break;
    }
    }
    return out;
}

#endif
