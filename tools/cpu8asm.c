/*
 * cpu8asm - two-pass assembler for cpu8
 * SPDX-License-Identifier: Apache-2.0
 *
 *   cpu8asm [-f bin|hex] [-l listing.txt] -o out.bin prog.asm
 *
 * Syntax (one statement per line, ';' starts a comment, case-insensitive):
 *
 *   label:  LDI 0x10        ; numbers: 12, 0x0c, 0b1100, 'a', '\n'
 *           LDA table,X     ; indexed: address + X
 *           CALL print      ; subroutine, RET returns
 *   N = 10                  ; constant (also: .equ N, 10)
 *           .org 0x100      ; set location counter
 *   table:  .byte 1, 2, N*3 ; bytes;  .word = 2 bytes, high byte first
 *   msg:    .asciz "hi\n"   ; string + NUL (.ascii: no NUL)
 *           .space 16       ; 16 zero bytes (.space n, fill)
 *           .align 2
 *           .include "lib/print.asm"   ; relative to this file
 *
 * Expressions: + - * / % ( ), unary -, <x (low byte), >x (x >> 8),
 * '$' = address of the current statement.
 *
 * Output: bin = raw image from address 0 up to the last byte used
 *         hex = 4096 lines, one byte each (for Verilog $readmemh)
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cpu8isa.h"

#define MAX_LINES 8192
#define MAX_SYMS  1024
#define NAME_LEN  48

struct sym {
    char name[NAME_LEN];
    int value;
    int defined;      /* pass in which it was last defined */
};

static struct sym syms[MAX_SYMS];
static int nsyms;

static const char *src_name;
static int line_no;
static int pass;           /* 1 or 2 */
static int here;           /* address of the current statement ('$') */

static unsigned char mem[CPU8_MEM_SIZE];
static unsigned char used[CPU8_MEM_SIZE];
static int top;            /* highest address written + 1 */
static FILE *listing;

static void fail(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s:%d: error: ", src_name, line_no);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void lower(char *s)
{
    for (; *s; s++)
        *s = (char)tolower((unsigned char)*s);
}

static struct sym *find_sym(const char *name)
{
    for (int i = 0; i < nsyms; i++)
        if (strcmp(syms[i].name, name) == 0)
            return &syms[i];
    return NULL;
}

static void define_sym(const char *name, int value, int is_label)
{
    struct sym *s = find_sym(name);
    if (s && s->defined == pass && is_label)
        fail("duplicate label '%s'", name);
    if (!s) {
        if (nsyms == MAX_SYMS)
            fail("too many symbols");
        s = &syms[nsyms++];
        snprintf(s->name, NAME_LEN, "%s", name);
    }
    s->value = value;
    s->defined = pass;
}

/* ---------------- expressions ---------------- */

static const char *ep;     /* parse pointer */

static void skip_ws(void)
{
    while (*ep == ' ' || *ep == '\t')
        ep++;
}

static int parse_char_escape(const char **pp)
{
    const char *p = *pp;
    int c = (unsigned char)*p++;
    if (c == '\\') {
        c = (unsigned char)*p++;
        switch (c) {
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        case '0': c = 0; break;
        case '\\': case '\'': case '"': break;
        default: fail("unknown escape '\\%c'", c);
        }
    }
    *pp = p;
    return c;
}

static int expr(void);

static int primary(void)
{
    skip_ws();
    if (*ep == '(') {
        ep++;
        int v = expr();
        skip_ws();
        if (*ep != ')')
            fail("missing ')'");
        ep++;
        return v;
    }
    if (*ep == '\'') {
        ep++;
        if (*ep == 0)
            fail("bad character literal");
        int v = parse_char_escape(&ep);
        if (*ep != '\'')
            fail("bad character literal");
        ep++;
        return v;
    }
    if (*ep == '$') {
        ep++;
        return here;
    }
    if (isdigit((unsigned char)*ep)) {
        char *end;
        long v;
        if (ep[0] == '0' && (ep[1] == 'b' || ep[1] == 'B'))
            v = strtol(ep + 2, &end, 2);
        else
            v = strtol(ep, &end, 0);   /* decimal, 0x hex, 0 octal */
        if (isalnum((unsigned char)*end) || *end == '_')
            fail("bad number near '%.10s'", ep);
        ep = end;
        return (int)v;
    }
    if (isalpha((unsigned char)*ep) || *ep == '_' || *ep == '.') {
        char name[NAME_LEN];
        int n = 0;
        while (isalnum((unsigned char)*ep) || *ep == '_' || *ep == '.') {
            if (n < NAME_LEN - 1)
                name[n++] = (char)tolower((unsigned char)*ep);
            ep++;
        }
        name[n] = 0;
        struct sym *s = find_sym(name);
        if (s && s->defined)
            return s->value;
        if (pass == 1)
            return 0;              /* forward reference, resolved in pass 2 */
        fail("unknown symbol '%s'", name);
    }
    fail("expected a value near '%.10s'", *ep ? ep : "end of line");
    return 0;
}

static int unary(void)
{
    skip_ws();
    if (*ep == '-') { ep++; return -unary(); }
    if (*ep == '+') { ep++; return unary(); }
    if (*ep == '<') { ep++; return unary() & 0xFF; }
    if (*ep == '>') { ep++; return (unary() >> 8) & 0xFF; }
    return primary();
}

static int product(void)
{
    int v = unary();
    for (;;) {
        skip_ws();
        char op = *ep;
        if (op != '*' && op != '/' && op != '%')
            return v;
        ep++;
        int r = unary();
        if (op == '*')
            v *= r;
        else if (r == 0)
            fail("division by zero");
        else
            v = (op == '/') ? v / r : v % r;
    }
}

static int expr(void)
{
    int v = product();
    for (;;) {
        skip_ws();
        if (*ep == '+') { ep++; v += product(); }
        else if (*ep == '-') { ep++; v -= product(); }
        else return v;
    }
}

/* Evaluate a whole operand string (must be fully consumed). */
static int eval(const char *s)
{
    ep = s;
    int v = expr();
    skip_ws();
    if (*ep)
        fail("unexpected '%s'", ep);
    return v;
}

/* ---------------- output ---------------- */

static int pc;             /* location counter */
static int list_count;     /* bytes listed on the current line */

static void emit(int byte)
{
    if (pc < 0 || pc >= CPU8_MEM_SIZE)
        fail("address 0x%X is outside the 4 KB memory", pc);
    if (pass == 2) {
        if (used[pc])
            fail("overlaps code/data already placed at 0x%03X", pc);
        used[pc] = 1;
        mem[pc] = (unsigned char)byte;
        if (pc + 1 > top)
            top = pc + 1;
        if (listing) {
            if (list_count == 0)
                fprintf(listing, "%03X  ", pc);
            if (list_count < 4)
                fprintf(listing, "%02X ", byte & 0xFF);
            list_count++;
        }
    }
    pc++;
}

/* ---------------- statements ---------------- */

/* Split "a, b, "c,d"" at top-level commas; returns count. */
static int split_args(char *s, char **out, int max)
{
    int n = 0, quote = 0;
    char *start = s;
    for (char *p = s;; p++) {
        if (*p == '"' && (p == s || p[-1] != '\\'))
            quote = !quote;
        else if (*p == '\'' && !quote && p[1] && p[2] == '\'') {
            p += 2;            /* skip 'x' so a ',' char literal is not a separator */
            continue;
        }
        if ((*p == ',' && !quote) || *p == 0) {
            if (n == max)
                fail("too many operands");
            char end = *p;
            *p = 0;
            out[n++] = start;
            if (end == 0)
                break;
            start = p + 1;
        }
    }
    for (int i = 0; i < n; i++) {        /* trim */
        while (isspace((unsigned char)*out[i]))
            out[i]++;
        char *e = out[i] + strlen(out[i]);
        while (e > out[i] && isspace((unsigned char)e[-1]))
            *--e = 0;
    }
    return n;
}

static void emit_string(const char *arg, int nul)
{
    const char *p = arg;
    if (*p != '"')
        fail("expected a quoted string");
    p++;
    while (*p && *p != '"')
        emit(parse_char_escape(&p));
    if (*p != '"')
        fail("unterminated string");
    if (p[1])
        fail("unexpected text after string");
    if (nul)
        emit(0);
}

static void directive(const char *name, char *args)
{
    char *a[4096];
    int n;

    if (!strcmp(name, ".org")) {
        pc = eval(args);
    } else if (!strcmp(name, ".byte") || !strcmp(name, ".word")) {
        int word = name[1] == 'w';
        n = split_args(args, a, 4096);
        for (int i = 0; i < n; i++) {
            if (a[i][0] == '"' && !word) {
                emit_string(a[i], 0);
                continue;
            }
            int v = eval(a[i]);
            if (pass == 2 && (word ? (v < -32768 || v > 65535) : (v < -128 || v > 255)))
                fail("value %d does not fit in a %s", v, word ? "word" : "byte");
            if (word)
                emit((v >> 8) & 0xFF);
            emit(v & 0xFF);
        }
    } else if (!strcmp(name, ".ascii") || !strcmp(name, ".asciz")) {
        emit_string(args, name[5] == 'z');
    } else if (!strcmp(name, ".space")) {
        n = split_args(args, a, 2);
        int count = eval(a[0]), fill = n > 1 ? eval(a[1]) : 0;
        if (count < 0)
            fail(".space count is negative");
        while (count--)
            emit(fill);
    } else if (!strcmp(name, ".align")) {
        int al = eval(args);
        if (al <= 0)
            fail(".align needs a positive value");
        while (pc % al)
            emit(0);
    } else if (!strcmp(name, ".equ")) {
        n = split_args(args, a, 2);
        if (n != 2)
            fail(".equ needs: name, value");
        lower(a[0]);
        define_sym(a[0], eval(a[1]), 0);
    } else {
        fail("unknown directive '%s'", name);
    }
}

static void instruction(const char *mnem, char *args)
{
    char upper[16];
    int n = 0;
    for (; mnem[n] && n < 15; n++)
        upper[n] = (char)toupper((unsigned char)mnem[n]);
    upper[n] = 0;

    /* "ea,X" selects the indexed form */
    int indexed = 0;
    char *comma = strrchr(args, ',');
    if (comma) {
        char *p = comma + 1;
        while (isspace((unsigned char)*p))
            p++;
        if ((p[0] == 'x' || p[0] == 'X') && (p[1] == 0 || isspace((unsigned char)p[1]))) {
            indexed = 1;
            *comma = 0;
        }
    }

    const struct cpu8_op *op = NULL;
    int name_known = 0;
    for (size_t i = 0; i < CPU8_NOPS; i++) {
        if (strcmp(cpu8_ops[i].name, upper))
            continue;
        name_known = 1;
        if ((cpu8_ops[i].kind == K_ADDRX) == indexed) {
            op = &cpu8_ops[i];
            break;
        }
    }
    if (!op) {
        if (name_known)
            fail("%s does not take an %s operand", upper, indexed ? "',X'" : "unindexed");
        fail("unknown instruction '%s'", mnem);
    }

    while (isspace((unsigned char)*args))
        args++;
    if (*args == '#' && op->kind == K_IMM)   /* optional '#' before immediates */
        args++;

    switch (op->kind) {
    case K_IMM: {
        if (!*args)
            fail("%s needs an immediate value", upper);
        int v = eval(args);
        if (pass == 2 && (v < -128 || v > 255))
            fail("immediate %d out of range (-128..255)", v);
        emit(op->code);
        emit(v & 0xFF);
        break;
    }
    case K_ADDR:
    case K_ADDRX: {
        if (!*args)
            fail("%s needs an address", upper);
        int ea = eval(args);
        if (pass == 2 && (ea < 0 || ea >= CPU8_MEM_SIZE))
            fail("address 0x%X out of range", ea);
        emit((op->code << 4) | ((ea >> 8) & 0xF));
        emit(ea & 0xFF);
        break;
    }
    case K_NONE:
        if (*args)
            fail("%s takes no operand", upper);
        emit(op->code);
        emit(0);
        break;
    }
}

static void statement(char *line)
{
    /* strip comment: ';' outside "strings" and 'c' literals */
    int quote = 0;
    for (char *p = line; *p; p++) {
        if (*p == '"' && (p == line || p[-1] != '\\'))
            quote = !quote;
        else if (!quote && *p == '\'' && p[1] && (p[2] == '\'' || (p[1] == '\\' && p[3] == '\'')))
            p += (p[1] == '\\') ? 3 : 2;
        else if (!quote && *p == ';') {
            *p = 0;
            break;
        }
    }

    char *p = line;
    for (;;) {                                 /* labels */
        while (isspace((unsigned char)*p))
            p++;
        char *q = p;
        while (isalnum((unsigned char)*q) || *q == '_' || *q == '.')
            q++;
        if (q == p || *q != ':' || isdigit((unsigned char)*p))
            break;
        *q = 0;
        lower(p);
        define_sym(p, pc, 1);
        p = q + 1;
    }
    if (!*p)
        return;

    char *word = p;
    while (*p && !isspace((unsigned char)*p) && *p != '=')
        p++;
    char save = *p;
    *p = 0;
    char *rest = save ? p + 1 : p;
    if (save == '=') {
        rest = p + 1;
    } else {
        while (isspace((unsigned char)*rest))
            rest++;
    }

    here = pc;
    if (save == '=' || rest[0] == '=') {       /* NAME = expr */
        if (rest[0] == '=' && save != '=')
            rest++;
        lower(word);
        define_sym(word, eval(rest), 0);
        return;
    }

    char *e = rest + strlen(rest);             /* trim right */
    while (e > rest && isspace((unsigned char)e[-1]))
        *--e = 0;

    if (word[0] == '.') {
        lower(word);
        directive(word, rest);
    } else {
        instruction(word, rest);
    }
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "%s: read error\n", path);
        exit(1);
    }
    buf[n] = 0;
    fclose(f);
    return buf;
}

/* ---------------- source loading (.include) ---------------- */

static char *lines[MAX_LINES];
static const char *line_file[MAX_LINES];
static int line_num[MAX_LINES];
static int nlines;

/* Read a file into the line list, expanding .include "file" (relative to
 * the including file's directory). */
static void load_file(const char *path, int depth)
{
    if (depth > 8)
        fail("includes nested too deeply");
    char *text = read_file(path);
    int n = 0;
    for (char *p = text; *p;) {
        char *nl = strchr(p, '\n');
        if (nl) {
            *nl = 0;
            if (nl > p && nl[-1] == '\r')
                nl[-1] = 0;
        }
        n++;
        char *q = p;
        while (isspace((unsigned char)*q))
            q++;
        if (strncasecmp(q, ".include", 8) == 0 && isspace((unsigned char)q[8])) {
            char *a = strchr(q, '"'), *b = a ? strchr(a + 1, '"') : NULL;
            src_name = path;
            line_no = n;
            if (!a || !b)
                fail(".include needs a quoted file name");
            *b = 0;
            const char *slash = strrchr(path, '/');
            size_t dir = slash ? (size_t)(slash - path + 1) : 0;
            char *inc = malloc(dir + strlen(a + 1) + 1);
            if (!inc)
                fail("out of memory");
            if (a[1] == '/')
                strcpy(inc, a + 1);
            else {
                memcpy(inc, path, dir);
                strcpy(inc + dir, a + 1);
            }
            load_file(inc, depth + 1);
        } else {
            if (nlines == MAX_LINES)
                fail("too many lines");
            line_file[nlines] = path;
            line_num[nlines] = n;
            lines[nlines++] = p;
        }
        if (!nl)
            break;
        p = nl + 1;
    }
}

static void usage(void)
{
    fprintf(stderr,
            "usage: cpu8asm [-f bin|hex] [-l listing] -o output prog.asm\n"
            "  -o FILE   output file\n"
            "  -f FMT    bin (raw image from 0x000, default) or hex (4096 lines\n"
            "            for $readmemh; default when the output ends in .hex)\n"
            "  -l FILE   write a listing (address, bytes, source)\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *out = NULL, *fmt = NULL, *list = NULL;
    int i;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
            usage();
        if (i + 1 >= argc)
            usage();
        if (!strcmp(argv[i], "-o"))
            out = argv[++i];
        else if (!strcmp(argv[i], "-f"))
            fmt = argv[++i];
        else if (!strcmp(argv[i], "-l"))
            list = argv[++i];
        else
            usage();
    }
    if (i != argc - 1 || !out)
        usage();
    src_name = argv[i];
    if (!fmt) {
        size_t n = strlen(out);
        fmt = (n > 4 && !strcmp(out + n - 4, ".hex")) ? "hex" : "bin";
    }
    if (strcmp(fmt, "bin") && strcmp(fmt, "hex"))
        usage();

    load_file(src_name, 0);

    if (list && !(listing = fopen(list, "w"))) {
        perror(list);
        return 1;
    }

    static char buf[16384];
    for (pass = 1; pass <= 2; pass++) {
        pc = 0;
        for (int l = 0; l < nlines; l++) {
            src_name = line_file[l];
            line_no = line_num[l];
            if (strlen(lines[l]) >= sizeof buf)
                fail("line too long");
            strcpy(buf, lines[l]);
            list_count = 0;
            statement(buf);
            if (pass == 2 && listing) {
                if (list_count == 0)
                    fprintf(listing, "     ");
                for (int k = list_count < 4 ? list_count : 4; k < 4; k++)
                    fprintf(listing, "   ");
                fprintf(listing, "%s%s\n", list_count > 4 ? "+" : " ", lines[l]);
            }
        }
    }
    if (listing)
        fclose(listing);

    FILE *f = fopen(out, "wb");
    if (!f) {
        perror(out);
        return 1;
    }
    if (!strcmp(fmt, "hex")) {
        for (int a = 0; a < CPU8_MEM_SIZE; a++)
            fprintf(f, "%02x\n", mem[a]);
    } else {
        fwrite(mem, 1, (size_t)top, f);
    }
    if (fclose(f)) {
        perror(out);
        return 1;
    }
    if (top > CPU8_STACK_PAGE)
        fprintf(stderr, "%s: warning: program uses 0x%03X-0x%03X, which is also the stack page\n",
                argv[i], CPU8_STACK_PAGE, top - 1);
    return 0;
}
