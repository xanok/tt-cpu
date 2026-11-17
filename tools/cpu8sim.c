/*
 * cpu8sim - instruction-level simulator for cpu8
 * SPDX-License-Identifier: Apache-2.0
 *
 *   cpu8sim [options] prog.bin
 *
 * Loads the raw image at address 0 (rest of the 4 KB memory is zero), runs
 * until HLT and prints every value written by OUT. The clock count follows
 * the RTL (src/cpu.v) exactly: 83 clocks per instruction, plus 65 for a
 * 1-byte memory access, 81 for CALL/RET and 17 per STRLEN character.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu8isa.h"

/* clock counts, derived from the RTL state machine */
#define CLK_INSTR  83   /* FETCH 1 + 40-bit SPI read 80 + FWAIT exit 1 + EXEC 1 */
#define CLK_MEM1   65   /* 32-bit SPI transaction 64 + MWAIT exit 1 */
#define CLK_MEM2   81   /* 40-bit SPI transaction (CALL, RET) */
#define CLK_STRCH  17   /* each extra STRLEN byte: 16 SPI clocks + 1 */

struct cpu {
    unsigned char mem[CPU8_MEM_SIZE];
    unsigned pc;          /* 12 bits */
    unsigned char a, x, sp, z, c;
    unsigned char out, in;
    int halted;
    unsigned long long instrs, clocks;
};

static unsigned char rd(struct cpu *s, unsigned addr)
{
    return s->mem[addr & 0xFFF];
}

static void wr(struct cpu *s, unsigned addr, unsigned char v)
{
    s->mem[addr & 0xFFF] = v;
}

/* Text form of the instruction at addr, for traces. */
static void disasm(const struct cpu *s, unsigned addr, char *buf, size_t n)
{
    unsigned char b0 = s->mem[addr & 0xFFF], lo = s->mem[(addr + 1) & 0xFFF];
    unsigned op = b0 >> 4, ea = ((b0 & 0xF) << 8) | lo;
    for (size_t i = 0; i < CPU8_NOPS; i++) {
        const struct cpu8_op *o = &cpu8_ops[i];
        if ((o->kind == K_IMM || o->kind == K_NONE) && o->code == b0) {
            if (o->kind == K_IMM)
                snprintf(buf, n, "%-6s 0x%02X", o->name, lo);
            else
                snprintf(buf, n, "%s", o->name);
            return;
        }
        if ((o->kind == K_ADDR || o->kind == K_ADDRX) && o->code == op) {
            snprintf(buf, n, "%-6s 0x%03X%s", o->name, ea, o->kind == K_ADDRX ? ",X" : "");
            return;
        }
    }
    snprintf(buf, n, "NOP    (%02X %02X)", b0, lo);
}

static void set_z(struct cpu *s, unsigned char v)
{
    s->z = (v == 0);
}

/* Execute one instruction. Returns the OUT value written, or -1. */
static int step(struct cpu *s)
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
        case 0x5: s->halted = 1; break;                                     /* HLT */
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
        s->clocks += CLK_MEM1;
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

static int parse_num(const char *s, long *v)
{
    char *end;
    *v = strtol(s, &end, 0);
    return *s && !*end;
}

static void usage(void)
{
    fprintf(stderr,
            "usage: cpu8sim [options] prog.bin\n"
            "  -i VALUE      input port value read by IN (default 0)\n"
            "  -n COUNT      stop after COUNT instructions (default 10000000)\n"
            "  -t            trace every instruction\n"
            "  -a            print OUT values as text (characters)\n"
            "  -q            do not print OUT values\n"
            "  -d ADDR:LEN   dump memory at the end (repeatable)\n"
            "  -s            print a summary (registers, instructions, clocks)\n"
            "  -f HZ         clock frequency for the time estimate (default 10 MHz)\n"
            "exit status: 0 halted, 1 error, 3 instruction limit reached\n");
    exit(1);
}

int main(int argc, char **argv)
{
    static struct cpu s;
    long v, limit = 10000000, hz = 10000000;
    int trace = 0, text = 0, quiet = 0, summary = 0, ndump = 0;
    long dump_addr[16], dump_len[16];
    int i;

    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        const char *o = argv[i];
        if (!strcmp(o, "-t")) trace = 1;
        else if (!strcmp(o, "-a")) text = 1;
        else if (!strcmp(o, "-q")) quiet = 1;
        else if (!strcmp(o, "-s")) summary = 1;
        else if (!strcmp(o, "-h") || !strcmp(o, "--help")) usage();
        else if (i + 1 < argc && !strcmp(o, "-i") && parse_num(argv[i + 1], &v) && v >= 0 && v < 256)
            s.in = (unsigned char)v, i++;
        else if (i + 1 < argc && !strcmp(o, "-n") && parse_num(argv[i + 1], &limit) && limit > 0)
            i++;
        else if (i + 1 < argc && !strcmp(o, "-f") && parse_num(argv[i + 1], &hz) && hz > 0)
            i++;
        else if (i + 1 < argc && !strcmp(o, "-d") && ndump < 16) {
            char *colon = strchr(argv[++i], ':');
            if (!colon)
                usage();
            *colon = 0;
            if (!parse_num(argv[i], &dump_addr[ndump]) || !parse_num(colon + 1, &dump_len[ndump]) ||
                dump_addr[ndump] < 0 || dump_len[ndump] < 1 ||
                dump_addr[ndump] + dump_len[ndump] > CPU8_MEM_SIZE)
                usage();
            ndump++;
        } else {
            usage();
        }
    }
    if (i != argc - 1)
        usage();

    FILE *f = fopen(argv[i], "rb");
    if (!f) {
        perror(argv[i]);
        return 1;
    }
    size_t n = fread(s.mem, 1, sizeof s.mem, f);
    if (fgetc(f) != EOF) {
        fprintf(stderr, "%s: larger than %d bytes\n", argv[i], CPU8_MEM_SIZE);
        return 1;
    }
    fclose(f);
    if (n == 0) {
        fprintf(stderr, "%s: empty file\n", argv[i]);
        return 1;
    }

    if (trace)
        fprintf(stderr, " PC  instruction        A  X  SP Z C\n");
    while (!s.halted && (long)s.instrs < limit) {
        char dis[32];
        unsigned at = s.pc;
        if (trace)
            disasm(&s, at, dis, sizeof dis);
        int out = step(&s);
        if (trace)
            fprintf(stderr, "%03X  %-17s  %02X %02X %02X %d %d\n", at, dis, s.a, s.x, s.sp, s.z, s.c);
        if (out >= 0 && !quiet) {
            if (text)
                putchar(out);
            else
                printf("%u\n", (unsigned)out);
            fflush(stdout);
        }
    }
    if (text && !quiet)
        fflush(stdout);

    for (int d = 0; d < ndump; d++) {
        for (long a = 0; a < dump_len[d]; a++) {
            if (a % 16 == 0)
                printf("%s%03lX:", a ? "\n" : "", dump_addr[d] + a);
            printf(" %02X", s.mem[dump_addr[d] + a]);
        }
        printf("\n");
    }
    if (summary || !s.halted)
        fprintf(stderr, "%s after %llu instructions, %llu clocks (%.3f ms at %.1f MHz)\n"
                        "A=%02X X=%02X SP=%02X Z=%d C=%d PC=%03X OUT=%02X\n",
                s.halted ? "halted" : "instruction limit reached", s.instrs, s.clocks,
                s.clocks * 1000.0 / hz, hz / 1e6, s.a, s.x, s.sp, s.z, s.c, s.pc, s.out);
    return s.halted ? 0 : 3;
}
