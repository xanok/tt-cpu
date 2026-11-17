/*
 * cpu8load - load and run a cpu8 program on the Tiny Tapeout demo board.
 *
 * Talks to the RP2040 firmware in ../rp2040 (protocol: rp2040/README.md)
 * over its USB serial port. Plain C99 + POSIX, no other libraries:
 *
 *   gcc -std=c99 -Wall -Wextra -O2 -o cpu8load cpu8load.c
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define PROG_MAX   4096  /* cpu8 address space; 0xF00-0xFFF is the stack */
#define CHUNK      128   /* bytes per W / R command (firmware limit) */
#define MAX_DUMPS  16
#define REPLY_MS   2000  /* normal command timeout */

static int fd = -1;
static int verbose;
static int show_events;     /* -m: print EV UO lines */
static int seen_halt;       /* got an EV HALT */
static int out_mode;        /* -a: OUT bytes as text, -n: as numbers, 0: not shown */
static FILE *info;          /* status messages: stdout, or stderr with -a / -n */

/* ---------------------------------------------------------------- utils */

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("cpu8load: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Number: decimal or 0x hex. Returns 0 on success. */
static int parse_num(const char *s, unsigned long max, unsigned long *out)
{
    char *end;
    int base = (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 16 : 10;
    if (*s == '\0' || *s == '-' || *s == '+')
        return -1;
    errno = 0;
    *out = strtoul(s, &end, base);
    return (errno || *end != '\0' || *out > max) ? -1 : 0;
}

static unsigned long num_arg(const char *opt, const char *s, unsigned long max)
{
    unsigned long v;
    if (parse_num(s, max, &v))
        die("bad value '%s' for %s (decimal or 0x hex, max %lu)", s, opt, max);
    return v;
}

/* --------------------------------------------------------------- serial */

static void serial_open(const char *dev)
{
    struct termios t;
    fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0)
        die("cannot open %s: %s", dev, strerror(errno));
    if (tcgetattr(fd, &t))
        die("%s is not a serial port: %s", dev, strerror(errno));
    t.c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
                             ICRNL | IXON | IXOFF);
    t.c_oflag &= ~(tcflag_t)OPOST;
    t.c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t.c_cflag &= ~(tcflag_t)(CSIZE | PARENB | CSTOPB);
    t.c_cflag |= CS8 | CREAD | CLOCAL;
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    cfsetispeed(&t, B115200);  /* ignored by USB CDC, but must be valid */
    cfsetospeed(&t, B115200);
    if (tcsetattr(fd, TCSANOW, &t))
        die("cannot configure %s: %s", dev, strerror(errno));
    tcflush(fd, TCIOFLUSH);
}

static void send_line(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (verbose)
        fprintf(stderr, "> %s\n", buf);
    buf[n++] = '\n';
    for (int off = 0; off < n;) {
        ssize_t w = write(fd, buf + off, (size_t)(n - off));
        if (w < 0 && errno != EINTR && errno != EAGAIN)
            die("write to device failed: %s", strerror(errno));
        if (w > 0)
            off += (int)w;
    }
}

/* Read one line (without CR/LF). Returns 1, or 0 on timeout. */
static int read_line(char *line, size_t size, int timeout_ms)
{
    static char rx[1024];
    static size_t rx_len;
    long long deadline = now_ms() + timeout_ms;

    for (;;) {
        char *nl = memchr(rx, '\n', rx_len);
        if (nl) {
            size_t n = (size_t)(nl - rx);
            size_t keep = n < size - 1 ? n : size - 1;
            memcpy(line, rx, keep);
            line[keep] = '\0';
            if (keep > 0 && line[keep - 1] == '\r')
                line[keep - 1] = '\0';
            rx_len -= n + 1;
            memmove(rx, nl + 1, rx_len);
            if (verbose)
                fprintf(stderr, "< %s\n", line);
            return 1;
        }
        if (rx_len == sizeof rx)
            rx_len = 0;  /* garbage without newlines: drop it */

        long long left = deadline - now_ms();
        if (left <= 0)
            return 0;
        struct pollfd p = { fd, POLLIN, 0 };
        int r = poll(&p, 1, (int)left);
        if (r < 0 && errno != EINTR)
            die("poll failed: %s", strerror(errno));
        if (r > 0) {
            if (p.revents & (POLLERR | POLLHUP | POLLNVAL))
                die("device disconnected");
            ssize_t n = read(fd, rx + rx_len, sizeof rx - rx_len);
            if (n < 0 && errno != EINTR && errno != EAGAIN)
                die("read from device failed: %s", strerror(errno));
            if (n > 0)
                rx_len += (size_t)n;
        }
    }
}

/* ------------------------------------------------------------- protocol */

static void handle_event(const char *line)
{
    unsigned v;
    unsigned long t;
    if (sscanf(line, "EV UO %x %lu", &v, &t) == 2) {
        if (show_events)
            printf("%10.3f ms  uo_out = 0x%02X (%3u)\n", t / 1000.0, v, v);
    } else if (!strncmp(line, "EV OUT ", 7)) {
        /* bytes the program wrote with OUT, captured on the OUT_STROBE pin */
        for (const char *p = line + 7; p[0] && p[1]; p += 2) {
            if (sscanf(p, "%2x", &v) != 1)
                break;
            if (out_mode == 'a')
                putchar((int)v);
            else if (out_mode == 'n')
                printf("%u\n", v);
        }
    } else if (sscanf(line, "EV HALT %lu", &t) == 1) {
        seen_halt = 1;
        if (show_events)
            printf("%10.3f ms  HALTED\n", t / 1000.0);
    }
    fflush(stdout);
}

/* Handle one incoming line. Returns 1 for OK, -1 for ERR, 0 for others. */
static int classify(const char *line, char *reply, size_t size)
{
    if (!strncmp(line, "EV ", 3)) {
        handle_event(line);
        return 0;
    }
    if (!strcmp(line, "OK") || !strncmp(line, "OK ", 3)) {
        snprintf(reply, size, "%s", line[2] ? line + 3 : "");
        return 1;
    }
    if (!strncmp(line, "ERR", 3)) {
        snprintf(reply, size, "%s", line[3] ? line + 4 : "");
        return -1;
    }
    return 0;  /* '#' help text or noise */
}

/* Send a command and wait for its OK; dies on ERR or timeout. */
static void command(char *reply, size_t size, int timeout_ms, const char *fmt, ...)
{
    char cmd[512], line[600], dummy[8];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    if (!reply) {
        reply = dummy;
        size = sizeof dummy;
    }
    send_line("%s", cmd);
    long long deadline = now_ms() + timeout_ms;
    for (;;) {
        long long left = deadline - now_ms();
        if (left <= 0 || !read_line(line, sizeof line, (int)left))
            die("no reply from the device to '%.20s' (wrong port or firmware?)", cmd);
        int r = classify(line, reply, size);
        if (r > 0)
            return;
        if (r < 0)
            die("device rejected '%.40s': %s", cmd, reply);
    }
}

static void sync_device(const char *dev)
{
    char line[600], reply[100];
    /* Finish any half-sent line and throw away stale output. */
    send_line("");
    while (read_line(line, sizeof line, 100))
        ;
    for (int attempt = 0; attempt < 3; attempt++) {
        send_line("PING");
        long long deadline = now_ms() + 1000;
        while (now_ms() < deadline && read_line(line, sizeof line, 200)) {
            if (classify(line, reply, sizeof reply) > 0 &&
                !strncmp(reply, "cpu8-loader", 11))
                return;
        }
    }
    die("%s does not answer like the cpu8 loader firmware", dev);
}

struct status {
    unsigned uo, halted, rst;
    unsigned long clk, t_us, lost;
};

static struct status get_status(void)
{
    char reply[300];
    const char *p;
    struct status s = { 0, 0, 0, 0, 0, 0 };
    command(reply, sizeof reply, REPLY_MS, "ST");
    if ((p = strstr(reply, "uo=")))     s.uo = (unsigned)strtoul(p + 3, NULL, 16);
    if ((p = strstr(reply, "halted="))) s.halted = (unsigned)strtoul(p + 7, NULL, 10);
    if ((p = strstr(reply, "rst=")))    s.rst = (unsigned)strtoul(p + 4, NULL, 10);
    if ((p = strstr(reply, "clk=")))    s.clk = strtoul(p + 4, NULL, 10);
    if ((p = strstr(reply, " t=")))     s.t_us = strtoul(p + 3, NULL, 10);
    if ((p = strstr(reply, "lost=")))   s.lost = strtoul(p + 5, NULL, 10);
    return s;
}

static void read_ram(unsigned addr, unsigned len, unsigned char *out)
{
    char reply[300];
    while (len > 0) {
        unsigned n = len < CHUNK ? len : CHUNK;
        command(reply, sizeof reply, REPLY_MS, "R 0x%04X %u", addr, n);
        if (strlen(reply) != 2 * n)
            die("short RAM read at 0x%04X", addr);
        for (unsigned i = 0; i < n; i++) {
            unsigned v;
            if (sscanf(reply + 2 * i, "%2x", &v) != 1)
                die("bad hex in RAM read at 0x%04X", addr);
            out[i] = (unsigned char)v;
        }
        addr += n;
        len -= n;
        out += n;
    }
}

static void dump_ram(unsigned addr, unsigned len)
{
    unsigned char *buf = malloc(len);
    if (!buf)
        die("out of memory");
    read_ram(addr, len, buf);
    printf("RAM 0x%04X..0x%04X:\n", addr, addr + len - 1);
    for (unsigned i = 0; i < len; i += 16) {
        printf("  %04X:", addr + i);
        for (unsigned j = 0; j < 16; j++)
            if (i + j < len)
                printf(" %02X", buf[i + j]);
            else
                printf("   ");
        printf("  |");
        for (unsigned j = 0; j < 16 && i + j < len; j++)
            putchar(buf[i + j] >= 32 && buf[i + j] < 127 ? buf[i + j] : '.');
        printf("|\n");
    }
    free(buf);
}

/* ------------------------------------------------------------------ main */

static void usage(FILE *f)
{
    fputs(
"usage: cpu8load [options] prog.bin\n"
"Load a raw cpu8 binary (at address 0, max 4096 bytes) into the TT demo\n"
"board's emulated SPI RAM, start the CPU and report uo_out.\n"
"\n"
"  -d DEV     serial device of the board (default /dev/ttyACM0)\n"
"  -p ADDR    select this Tiny Tapeout project address first; without -p\n"
"             the currently selected project is used\n"
"  -f HZ      project clock in Hz (default 10000000)\n"
"  -i VAL     value for ui_in, the CPU's IN port (default 0)\n"
"  -s KHZ     set the RP2040 system clock first (default: firmware's 200000)\n"
"  -e 0|1     MISO_RISE strap: the CPU samples MISO at the end of the SCK\n"
"             high phase (0, default) or at the SCK rising edge (1)\n"
"  -w         wait until the CPU executes HLT (HALTED pin high)\n"
"  -a         print the bytes the program writes with OUT as text\n"
"             (captured on the OUT_STROBE pin; like cpu8sim -a); implies -w\n"
"  -n         print the OUT bytes as numbers, one per line; implies -w\n"
"             (with -a / -n, status messages go to stderr)\n"
"  -m         monitor: print every uo_out change while running\n"
"  -t SEC     timeout for -w, run time for -m alone (default 10)\n"
"  -r A:L     after the run, dump L bytes of RAM from address A\n"
"             (repeatable; e.g. -r 0x800:64)\n"
"  -v         show the serial protocol traffic\n"
"  -h         this help\n"
"\n"
"Numbers are decimal or 0x hex. Exit status: 0 ok, 1 error, 2 timeout.\n",
    f);
}

int main(int argc, char **argv)
{
    const char *dev = "/dev/ttyACM0";
    long project = -1;
    unsigned long clk = 10000000, ui = 0, sysclk = 0, timeout_s = 10, edge = 0;
    int wait_halt = 0, monitor = 0, ndumps = 0;
    unsigned dump_addr[MAX_DUMPS], dump_len[MAX_DUMPS];
    int opt;

    while ((opt = getopt(argc, argv, "d:p:f:i:s:e:wanmt:r:vh")) != -1) {
        switch (opt) {
        case 'd': dev = optarg; break;
        case 'p': project = (long)num_arg("-p", optarg, 1023); break;
        case 'f': clk = num_arg("-f", optarg, 100000000); break;
        case 'i': ui = num_arg("-i", optarg, 255); break;
        case 's': sysclk = num_arg("-s", optarg, 1000000); break;
        case 'e': edge = num_arg("-e", optarg, 1); break;
        case 'w': wait_halt = 1; break;
        case 'a': case 'n': out_mode = opt; wait_halt = 1; break;
        case 'm': monitor = 1; break;
        case 't': timeout_s = num_arg("-t", optarg, 86400); break;
        case 'r': {
            char *colon = strchr(optarg, ':');
            unsigned long a, l;
            if (ndumps == MAX_DUMPS)
                die("too many -r options (max %d)", MAX_DUMPS);
            if (!colon)
                die("-r needs ADDR:LEN, e.g. -r 0x800:64");
            *colon = '\0';
            a = num_arg("-r address", optarg, 0xFFFF);
            l = num_arg("-r length", colon + 1, 0x10000);
            if (l == 0 || a + l > 0x10000)
                die("-r range 0x%lX+%lu is outside the 64 KB RAM", a, l);
            dump_addr[ndumps] = (unsigned)a;
            dump_len[ndumps++] = (unsigned)l;
            break;
        }
        case 'v': verbose = 1; break;
        case 'h': usage(stdout); return 0;
        default:  usage(stderr); return 1;
        }
    }
    if (optind != argc - 1) {
        usage(stderr);
        return 1;
    }
    if (clk == 0)
        die("-f must be more than 0 Hz");
    info = out_mode ? stderr : stdout;

    /* Read the program. */
    const char *path = argv[optind];
    unsigned char prog[PROG_MAX + 1];
    FILE *f = fopen(path, "rb");
    if (!f)
        die("cannot open %s: %s", path, strerror(errno));
    size_t size = fread(prog, 1, sizeof prog, f);
    int read_err = ferror(f);
    fclose(f);
    if (read_err)
        die("cannot read %s", path);
    if (size == 0)
        die("%s is empty", path);
    if (size > PROG_MAX)
        die("%s is too big: cpu8 has a 4096-byte address space", path);
    if (size > 0xF00)
        fprintf(stderr, "cpu8load: warning: program reaches into the stack page "
                        "0xF00-0xFFF\n");

    serial_open(dev);
    sync_device(dev);

    char reply[300];
    if (sysclk) {
        command(reply, sizeof reply, REPLY_MS, "SYSCLK %lu", sysclk);
        fprintf(info, "RP2040 clock: %s\n", reply);
    }
    command(NULL, 0, REPLY_MS, "RST");
    if (project >= 0) {
        fprintf(info, "selecting project %ld\n", project);
        command(NULL, 0, 5000 + 3 * (int)project, "SEL %ld", project);
    } else {
        command(NULL, 0, REPLY_MS, "ATTACH");
    }

    /* Upload: clear the 4 KB the CPU can see, write, read back. */
    command(NULL, 0, REPLY_MS, "FILL 0 %d 0", PROG_MAX);
    for (size_t off = 0; off < size; off += CHUNK) {
        char hex[2 * CHUNK + 1];
        size_t n = size - off < CHUNK ? size - off : CHUNK;
        for (size_t i = 0; i < n; i++)
            snprintf(hex + 2 * i, 3, "%02X", prog[off + i]);
        command(NULL, 0, REPLY_MS, "W 0x%04X %s", (unsigned)off, hex);
    }
    unsigned char check[PROG_MAX];
    read_ram(0, (unsigned)size, check);
    for (size_t i = 0; i < size; i++)
        if (check[i] != prog[i])
            die("verify failed at 0x%03X: wrote %02X, read %02X",
                (unsigned)i, prog[i], check[i]);
    fprintf(info, "loaded %zu bytes from %s\n", size, path);

    command(reply, sizeof reply, REPLY_MS, "CLK %lu", clk);
    unsigned long real_clk = strtoul(reply, NULL, 10);
    if (real_clk != clk)
        fprintf(info, "clock: asked %lu Hz, running at %lu Hz\n", clk, real_clk);
    command(NULL, 0, REPLY_MS, "UI %lu", ui);
    command(NULL, 0, REPLY_MS, "EDGE %lu", edge);
    if (monitor) {
        show_events = 1;
        command(NULL, 0, REPLY_MS, "MON 1");
    }
    command(NULL, 0, REPLY_MS, "GO");
    fprintf(info, "running at %lu Hz, ui_in = 0x%02lX\n", real_clk, ui);
    fflush(stdout);

    /* Wait for HLT and/or monitor uo_out. */
    int rc = 0;
    struct status st = { 0, 0, 0, 0, 0, 0 };
    if (wait_halt || monitor) {
        long long deadline = now_ms() + (long long)timeout_s * 1000;
        long long next_poll = 0;
        char line[600];
        for (;;) {
            if (wait_halt && now_ms() >= next_poll) {
                st = get_status();  /* poll too, in case an event was missed */
                if (st.halted)
                    break;
                next_poll = now_ms() + (monitor ? 500 : 50);
            }
            if (seen_halt && wait_halt)
                break;
            if (now_ms() >= deadline) {
                if (wait_halt) {
                    fprintf(stderr, "cpu8load: timeout: CPU did not halt within %lu s\n",
                            timeout_s);
                    rc = 2;
                }
                break;
            }
            if (read_line(line, sizeof line, 20))
                classify(line, reply, sizeof reply);
        }
    }
    st = get_status();  /* also flushes the remaining EV OUT lines */
    fflush(stdout);
    if (monitor)
        command(NULL, 0, REPLY_MS, "MON 0");
    if (st.lost)
        fprintf(stderr, "cpu8load: warning: %lu output bytes lost (capture buffer full)\n",
                st.lost);
    fprintf(info, "uo_out = 0x%02X (%u)  halted = %s  (%.3f ms since start)\n", st.uo, st.uo,
           st.halted ? "yes" : "no", st.t_us / 1000.0);

    for (int i = 0; i < ndumps; i++)
        dump_ram(dump_addr[i], dump_len[i]);
    close(fd);
    return rc;
}
