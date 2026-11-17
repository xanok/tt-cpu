/*
 * cpu8sim - instruction-level simulator for cpu8
 * SPDX-License-Identifier: Apache-2.0
 *
 *   cpu8sim [options] prog.bin
 *
 * Loads the raw image at address 0 (rest of the 4 KB memory is zero), runs
 * until HLT and prints every value written by OUT. The clock count follows
 * the RTL (src/cpu.v) exactly: 88 clocks per instruction, plus 72 for a
 * 1-byte memory access, 88 for CALL/RET, and 73 + 17 per character for
 * STRLEN. Each SPI transaction is followed by 8 clocks with CS high.
 *
 * Input for programs that use the mailbox (programs/lib/input.asm) comes
 * from -I TEXT or, with -k, from standard input.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu8isa.h"

#include "cpu8core.h"

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

/* Mailbox input: -I text (fed as soon as the flag is free, like the RP2040
 * firmware does) or -k (read from stdin only when the program asks). */
static unsigned char *in_text;
static size_t in_len, in_pos;
static int in_stdin;

/* Copy s, replacing the escapes \n \r \t \0 \\ \xHH. */
static void set_input(const char *s)
{
    in_text = malloc(strlen(s) + 1);
    if (!in_text)
        exit(1);
    for (in_len = 0; *s; s++) {
        int c = (unsigned char)*s;
        if (c == '\\' && s[1]) {
            switch (*++s) {
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case '0': c = 0; break;
            case 'x':
                if (isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
                    char h[3] = { s[1], s[2], 0 };
                    c = (int)strtol(h, NULL, 16);
                    s += 2;
                    break;
                }
                /* fall through */
            default: c = (unsigned char)*s; break;
            }
        }
        in_text[in_len++] = (unsigned char)c;
    }
}

/* Called before every instruction: put the next input byte in the mailbox. */
static void feed_input(struct cpu *s)
{
    int c;
    if (s->mem[CPU8_MBOX_FLAG] != 0)
        return;
    if (in_pos < in_len) {
        c = in_text[in_pos++];
    } else if (in_stdin) {
        /* only when the next instruction reads the (empty) flag */
        unsigned char b0 = s->mem[s->pc], lo = s->mem[(s->pc + 1) & 0xFFF];
        unsigned op = b0 >> 4, ea = ((b0 & 0xFu) << 8) | lo;
        if (op == 5)
            ea = (ea + s->x) & 0xFFF;
        if ((op != 1 && op != 5) || ea != CPU8_MBOX_FLAG)
            return;
        fflush(stdout);
        if ((c = getchar()) == EOF) {
            in_stdin = 0;
            return;
        }
    } else {
        return;
    }
    s->mem[CPU8_MBOX_DATA] = (unsigned char)c;
    s->mem[CPU8_MBOX_FLAG] = 1;
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
            "  -I TEXT       mailbox input (escapes \\n \\t \\r \\0 \\\\ \\xHH), see lib/input.asm\n"
            "  -k            mailbox input from standard input (interactive)\n"
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
        else if (!strcmp(o, "-k")) in_stdin = 1;
        else if (i + 1 < argc && !strcmp(o, "-I")) set_input(argv[++i]);
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
        feed_input(&s);
        int out = step(&s);
        if (trace)
            fprintf(stderr, "%03X  %-17s  %02X %03X %02X %d %d\n", at, dis, s.a, s.x, s.sp, s.z, s.c);
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
                        "A=%02X X=%03X SP=%02X Z=%d C=%d PC=%03X OUT=%02X\n",
                s.halted ? "halted" : "instruction limit reached", s.instrs, s.clocks,
                s.clocks * 1000.0 / hz, hz / 1e6, s.a, s.x, s.sp, s.z, s.c, s.pc, s.out);
    return s.halted ? 0 : 3;
}
