/*
 * cpu8dis - disassembler for cpu8 binaries (the "objdump -d" of cpu8)
 * SPDX-License-Identifier: Apache-2.0
 *
 *   cpu8dis [options] prog.bin
 *
 * A raw image has no symbols and mixes code and data, so by default the
 * disassembler FOLLOWS the code: it starts at address 0 and walks every
 * path through jumps, branches and calls. cpu8 has no computed jumps, so
 * this finds all the code a program can reach; everything else is shown as
 * data. Jump and call targets and the addresses of memory operands get
 * generated labels (L_ = jump target, F_ = called function, D_ = data).
 *
 * With -S the output is plain assembler source that cpu8asm turns back
 * into the same binary.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu8isa.h"

enum { DATA = 0, CODE, CODE2 };          /* what a byte is */
enum { LBL_JUMP = 1, LBL_CALL = 2, LBL_DATA = 4 };

static unsigned char mem[CPU8_MEM_SIZE];
static unsigned char kind[CPU8_MEM_SIZE + 2];
static unsigned char label[CPU8_MEM_SIZE + 2];
static unsigned size;                    /* bytes in the image */
static unsigned first, last;             /* range to print: [first, last) */

/* The table entry for an instruction, or NULL if it has no canonical text
 * (reserved encodings, or a no-operand instruction with a non-zero operand
 * byte); those are printed as .byte so the output assembles back exactly. */
static const struct cpu8_op *decode(unsigned b0, unsigned lo)
{
    for (size_t i = 0; i < CPU8_NOPS; i++) {
        const struct cpu8_op *o = &cpu8_ops[i];
        if (o->kind == K_IMM && o->code == b0)
            return o;
        if (o->kind == K_NONE && o->code == b0)
            return lo == 0 ? o : NULL;
        if ((o->kind == K_ADDR || o->kind == K_ADDRX) && o->code == (b0 >> 4))
            return o;
    }
    return NULL;
}

static int is_op(unsigned b0, unsigned op) { return (b0 >> 4) == op; }

/* Follow the code from addr, marking instructions and collecting labels. */
static void follow(unsigned addr)
{
    static unsigned stack[CPU8_MEM_SIZE];
    unsigned sp = 0;
    stack[sp++] = addr;
    while (sp) {
        unsigned pc = stack[--sp];
        while (pc + 1 < size && kind[pc] == DATA && kind[pc + 1] == DATA) {
            unsigned b0 = mem[pc], ea = ((b0 & 0xFu) << 8) | mem[pc + 1];
            kind[pc] = CODE;
            kind[pc + 1] = CODE2;
            if (is_op(b0, 0x9)) {                      /* JMP: no fall-through */
                label[ea] |= LBL_JUMP;
                pc = ea;
                continue;
            }
            if (is_op(b0, 0xA) || is_op(b0, 0xB) || is_op(b0, 0xC) || is_op(b0, 0x8)) {
                label[ea] |= is_op(b0, 0x8) ? LBL_CALL : LBL_JUMP;
                if (ea + 1 < size && sp < CPU8_MEM_SIZE)
                    stack[sp++] = ea;
            } else if (b0 == 0xD5 || b0 == 0xDC) {     /* HLT, RET */
                break;
            }
            pc += 2;
        }
    }
}

/* Memory operands of the instructions found: mark the addresses they use. */
static void mark_data_refs(void)
{
    for (unsigned pc = 0; pc + 1 < size; pc++) {
        if (kind[pc] != CODE)
            continue;
        const struct cpu8_op *o = decode(mem[pc], mem[pc + 1]);
        unsigned op = mem[pc] >> 4;
        if (o && (o->kind == K_ADDR || o->kind == K_ADDRX) && !(op >= 0x8 && op <= 0xC))
            label[((mem[pc] & 0xFu) << 8) | mem[pc + 1]] |= LBL_DATA;
    }
}

/* Name of the label at addr (it must have one). */
static void label_name(unsigned addr, char *buf, size_t n)
{
    snprintf(buf, n, "%c_%03X", label[addr] & LBL_CALL ? 'F' : label[addr] & LBL_JUMP ? 'L' : 'D', addr);
}

/* Text for an address operand: a label, label+1 (second byte of an
 * instruction, as in self-modifying code) or a number. */
static void addr_text(unsigned ea, char *buf, size_t n)
{
    if (ea < size && ea >= first && ea < last) {
        if (kind[ea] == CODE2 && ea > 0) {
            char base[16];
            label[ea - 1] |= LBL_DATA;         /* make sure the instruction has a label */
            label_name(ea - 1, base, sizeof base);
            snprintf(buf, n, "%s+1", base);
            return;
        }
        if (label[ea]) {
            label_name(ea, buf, n);
            return;
        }
    }
    snprintf(buf, n, "0x%03X", ea);
}

static const char *note(unsigned ea)
{
    if (ea == CPU8_MBOX_FLAG) return "input mailbox: flag";
    if (ea == CPU8_MBOX_DATA) return "input mailbox: data";
    if (ea >= CPU8_STACK_PAGE) return "stack page";
    return "";
}

/* One output line. In source mode only the label, text and comment. */
static void emit(int source, unsigned addr, unsigned nbytes, const char *text, const char *comment)
{
    char lbl[20] = "";
    if (label[addr] && addr >= first) {
        label_name(addr, lbl, sizeof lbl - 1);
        strcat(lbl, ":");
    }
    if (!source) {
        char hex[40] = "";
        for (unsigned i = 0; i < nbytes && i < 8; i++)
            snprintf(hex + 3 * i, 4, "%02X ", mem[addr + i]);
        printf("%03X:  %-24s ", addr, hex);
    }
    if (comment && *comment)
        printf("%-8s%-28s ; %s\n", lbl, text, comment);
    else
        printf("%-8s%s\n", lbl, text);
}

static void print_code(int source, unsigned pc)
{
    unsigned b0 = mem[pc], lo = mem[pc + 1], ea = ((b0 & 0xFu) << 8) | lo;
    const struct cpu8_op *o = decode(b0, lo);
    char text[64], opnd[32];
    const char *cmt = "";
    if (!o) {
        snprintf(text, sizeof text, ".byte  0x%02X, 0x%02X", b0, lo);
        cmt = (b0 >> 4) == 0xD ? "no-operand instruction with an operand byte, or reserved (NOP)"
                               : "reserved immediate function (NOP)";
    } else if (o->kind == K_IMM) {
        snprintf(text, sizeof text, "%-6s 0x%02X", o->name, lo);
        static char c[8];
        if (isprint(lo) && lo != ' ' && (b0 == 0x00 || b0 == 0x06)) {  /* LDI, CPI */
            snprintf(c, sizeof c, "'%c'", lo);
            cmt = c;
        }
    } else if (o->kind == K_NONE) {
        snprintf(text, sizeof text, "%s", o->name);
    } else {
        addr_text(ea, opnd, sizeof opnd);
        snprintf(text, sizeof text, "%-6s %s%s", o->name, opnd, o->kind == K_ADDRX ? ",X" : "");
        cmt = note(ea);
    }
    emit(source, pc, 2, text, cmt);
}

/* Data from addr up to end (no label inside): zeros as .space, the rest as
 * .byte lines of up to 8 bytes with the characters in a comment. */
static void print_data(int source, unsigned addr, unsigned end)
{
    while (addr < end) {
        unsigned n = 0;
        char text[80], cmt[24];
        while (addr + n < end && mem[addr + n] == 0)
            n++;
        if (n >= 4 || (n > 0 && addr + n == end && n > 1)) {
            snprintf(text, sizeof text, ".space %u", n);
            if (!source)                      /* the bytes column would be all zeros */
                printf("%03X:  %-24s ", addr, "00 ...");
            {
                char lbl[20] = "";
                if (label[addr]) {
                    label_name(addr, lbl, sizeof lbl - 1);
                    strcat(lbl, ":");
                }
                printf("%-8s%s\n", lbl, text);
            }
            addr += n;
            continue;
        }
        n = end - addr < 8 ? end - addr : 8;
        /* stop a line before a long run of zeros so it becomes .space */
        for (unsigned i = 1; i < n; i++) {
            unsigned z = 0;
            while (addr + i + z < end && mem[addr + i + z] == 0)
                z++;
            if (z >= 4) {
                n = i;
                break;
            }
        }
        int len = snprintf(text, sizeof text, ".byte  ");
        for (unsigned i = 0; i < n; i++) {
            len += snprintf(text + len, sizeof text - (size_t)len, "%s0x%02X", i ? ", " : "", mem[addr + i]);
            cmt[i] = isprint(mem[addr + i]) ? (char)mem[addr + i] : '.';
        }
        cmt[n] = 0;
        char quoted[32];
        snprintf(quoted, sizeof quoted, "\"%s\"", cmt);
        emit(source, addr, n, text, quoted);
        addr += n;
    }
}

static int parse_num(const char *s, unsigned long *v)
{
    char *end;
    *v = strtoul(s, &end, 0);
    return *s && !*end;
}

static void usage(void)
{
    fprintf(stderr,
            "usage: cpu8dis [options] prog.bin\n"
            "  -a            disassemble every byte pair as code (no code following)\n"
            "  -e ADDR       extra entry point for code following (repeatable)\n"
            "  -s ADDR       first address to print (default 0)\n"
            "  -n LEN        number of bytes to print (default: to the end of the file)\n"
            "  -S            assembler source only: cpu8asm turns it back into the same\n"
            "                binary (not with -s / -n)\n"
            "Labels: L_xxx jump target, F_xxx called function, D_xxx data; xxx = address.\n");
    exit(1);
}

int main(int argc, char **argv)
{
    int all = 0, source = 0, nentry = 0, have_range = 0, i;
    unsigned long v, start = 0, count = 0, entry[32];

    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        const char *o = argv[i];
        if (!strcmp(o, "-a")) all = 1;
        else if (!strcmp(o, "-S")) source = 1;
        else if (!strcmp(o, "-h") || !strcmp(o, "--help")) usage();
        else if (i + 1 < argc && !strcmp(o, "-e") && nentry < 32 && parse_num(argv[i + 1], &v) && v < CPU8_MEM_SIZE)
            entry[nentry++] = v, i++;
        else if (i + 1 < argc && !strcmp(o, "-s") && parse_num(argv[i + 1], &start) && start < CPU8_MEM_SIZE)
            have_range = 1, i++;
        else if (i + 1 < argc && !strcmp(o, "-n") && parse_num(argv[i + 1], &count) && count > 0)
            have_range = 1, i++;
        else usage();
    }
    if (i != argc - 1 || (source && have_range))
        usage();

    FILE *f = fopen(argv[i], "rb");
    if (!f) {
        perror(argv[i]);
        return 1;
    }
    size = (unsigned)fread(mem, 1, sizeof mem, f);
    if (fgetc(f) != EOF) {
        fprintf(stderr, "%s: larger than %d bytes\n", argv[i], CPU8_MEM_SIZE);
        return 1;
    }
    fclose(f);
    if (size == 0) {
        fprintf(stderr, "%s: empty file\n", argv[i]);
        return 1;
    }
    first = (unsigned)start;
    last = count && start + count < size ? (unsigned)(start + count) : size;
    if (first >= size) {
        fprintf(stderr, "%s: start address 0x%03X is past the end (0x%03X bytes)\n", argv[i], first, size);
        return 1;
    }

    if (all) {
        for (unsigned pc = first; pc + 1 < size; pc += 2) {
            unsigned b0 = mem[pc], ea = ((b0 & 0xFu) << 8) | mem[pc + 1];
            kind[pc] = CODE;
            kind[pc + 1] = CODE2;
            if (is_op(b0, 0x8))
                label[ea] |= LBL_CALL;
            else if (b0 >> 4 >= 0x9 && b0 >> 4 <= 0xC)
                label[ea] |= LBL_JUMP;
        }
    } else {
        follow(0);
        for (int e = 0; e < nentry; e++) {
            label[entry[e]] |= LBL_CALL;
            follow((unsigned)entry[e]);
        }
    }
    mark_data_refs();
    /* A label can only be printed at the start of an instruction or on a
     * data byte. References into the middle of an instruction are written
     * as label+1, so the instruction itself needs a label: settle that
     * before printing anything. */
    for (unsigned a = 1; a < size; a++)
        if (label[a] && kind[a] == CODE2) {
            label[a - 1] |= LBL_DATA;
            label[a] = 0;
        }
    /* labels outside the printed range or the image are shown as numbers */
    for (unsigned a = 0; a < CPU8_MEM_SIZE; a++)
        if (a < first || a >= last)
            label[a] = 0;

    if (source)
        printf("; disassembly of %s (%u bytes) by cpu8dis\n", argv[i], size);
    else {
        unsigned ncode = 0;
        for (unsigned k = 0; k < size; k++)
            ncode += kind[k] != DATA;
        if (all)
            printf("%s: %u bytes, every byte pair shown as code\n\n", argv[i], size);
        else
            printf("%s: %u bytes: %u of code reached from address 0, %u shown as data\n"
                   "(code that nothing jumps to or calls also appears as data: add -e ADDR, or use -a)\n\n",
                   argv[i], size, ncode, size - ncode);
    }
    if (first && !source)
        printf("        .org 0x%03X\n", first);

    unsigned a = first;
    while (a < last) {
        if (kind[a] == CODE && a + 1 < size) {
            if (!source && a > first && kind[a - 1] == DATA)
                printf("\n");
            print_code(source, a);
            a += 2;
        } else if (kind[a] == CODE2) {       /* range starts in the middle of an instruction */
            a++;
        } else {
            unsigned end = a + 1;
            while (end < last && kind[end] == DATA && !label[end])
                end++;
            if (!source && a > first && kind[a - 1] != DATA)
                printf("\n");
            print_data(source, a, end);
            a = end;
        }
    }
    return 0;
}
