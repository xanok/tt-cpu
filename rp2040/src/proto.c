// Command parser of the cpu8 loader. Every command line gets exactly one
// final reply line, "OK ..." or "ERR <reason>". Lines starting with "#" are
// comments (HELP text) and "EV ..." lines are asynchronous monitor events.
// The protocol is documented in ../README.md.

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hw.h"
#include "proto.h"

#define LINE_LEN    320  // longest accepted command line
#define MAX_XFER    128  // max bytes per W or R command
#define MAX_ARGS    8
#define MAX_PROJECT 1023
#define SYSCLK_MIN_KHZ  50000
#define SYSCLK_MAX_KHZ 250000

static char     line[LINE_LEN + 1];
static unsigned line_len;
static bool     line_too_long;

static struct {
    uint32_t clk_req;      // clock asked for by CLK (Hz)
    uint32_t clk_hz;       // clock actually running (Hz), 0 = stopped
    uint8_t  ui;
    int      sel;          // selected project address, -1 = none
    bool     in_reset;
    bool     monitor;
    uint64_t t_go;         // time of the last GO
    uint8_t  last_uo;
    bool     last_halted;
} st = { .sel = -1, .in_reset = true };

static void reply(const char *fmt, ...)
{
    char buf[LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    hw_puts(buf);
    hw_puts("\n");
}

static unsigned long since_go_us(void)
{
    return (unsigned long)(uint32_t)(hw_time_us() - st.t_go);
}

// Number: decimal, or hex with a 0x prefix.
static bool parse_num(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    unsigned base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    if (*s == '\0')
        return false;
    for (; *s; s++) {
        unsigned d;
        if (*s >= '0' && *s <= '9')
            d = (unsigned)(*s - '0');
        else if (base == 16 && *s >= 'a' && *s <= 'f')
            d = (unsigned)(*s - 'a' + 10);
        else if (base == 16 && *s >= 'A' && *s <= 'F')
            d = (unsigned)(*s - 'A' + 10);
        else
            return false;
        if (v > (UINT32_MAX - d) / base)
            return false;  // overflow
        v = v * base + d;
    }
    *out = v;
    return true;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Parse argument i as a number no larger than max. Replies ERR on failure.
static bool arg_num(char **argv, int i, uint32_t max, uint32_t *out)
{
    if (!parse_num(argv[i], out)) {
        reply("ERR bad number '%s'", argv[i]);
        return false;
    }
    if (*out > max) {
        reply("ERR %s out of range (max %lu)", argv[i], (unsigned long)max);
        return false;
    }
    return true;
}

static bool check_range(uint32_t addr, uint32_t len)
{
    if (addr >= RAM_SIZE || len > RAM_SIZE - addr) {
        reply("ERR range 0x%lx+%lu outside the 64 KB RAM",
              (unsigned long)addr, (unsigned long)len);
        return false;
    }
    return true;
}

static void hold_reset(void)
{
    hw_reset(true);
    st.in_reset = true;
}

static uint32_t max_clock(void)
{
    // spi-ram-emu READ works up to SCK = sys/10, and cpu8 runs SCK = clk/2.
    return hw_sys_hz() / 5;
}

static void cmd_help(void)
{
    static const char *const text[] = {
        "# PING                 -> OK cpu8-loader <version>",
        "# ST                   -> OK uo=.. halted=. rst=. clk=.. ui=.. sel=.. sys=.. mon=. t=..",
        "# SEL addr             select TT project (holds it in reset)",
        "# ATTACH | DETACH      drive / float ui_in and MISO without selecting",
        "# RST                  hold the project in reset",
        "# GO                   pulse reset, then run",
        "# CLK hz               project clock, 0 = stop -> OK <actual hz>",
        "# UI val               set ui_in",
        "# W addr hexbytes      write up to 128 bytes to RAM",
        "# R addr len           read up to 128 bytes -> OK <hex>",
        "# FILL addr len val    fill RAM",
        "# MON 0|1              stream EV UO <hex> <us> / EV HALT <us>",
        "# (always while running: EV OUT <hexbytes> = bytes written by OUT)",
        "# SYSCLK khz           RP2040 clock (holds reset) -> OK sys=.. clk=..",
        "# BOOT                 reboot to the UF2 bootloader",
        "# numbers: decimal or 0x hex; hexbytes: 2 hex digits per byte",
    };
    for (unsigned i = 0; i < sizeof text / sizeof text[0]; i++)
        reply("%s", text[i]);
    reply("OK");
}

static void cmd_status(void)
{
    reply("OK uo=%02X halted=%d rst=%d clk=%lu ui=%02X sel=%d sys=%lu mon=%d t=%lu lost=%lu",
          hw_get_uo(), hw_get_halted() ? 1 : 0, st.in_reset ? 1 : 0,
          (unsigned long)st.clk_hz, st.ui, st.sel, (unsigned long)hw_sys_hz(),
          st.monitor ? 1 : 0, st.in_reset ? 0ul : since_go_us(),
          (unsigned long)hw_out_lost());
}

static void cmd_clock(uint32_t hz)
{
    if (hz != 0 && hz > max_clock()) {
        reply("ERR %lu Hz too fast: max %lu Hz at sysclk %lu kHz (spi-ram-emu)",
              (unsigned long)hz, (unsigned long)max_clock(),
              (unsigned long)(hw_sys_hz() / 1000));
        return;
    }
    if (hz != 0 && hz < hw_min_clock()) {
        reply("ERR %lu Hz too slow: min %lu Hz", (unsigned long)hz,
              (unsigned long)hw_min_clock());
        return;
    }
    st.clk_req = hz;
    st.clk_hz = hw_set_clock(hz);
    reply("OK %lu", (unsigned long)st.clk_hz);
}

static void cmd_go(void)
{
    if (st.clk_hz == 0) {
        reply("ERR clock is stopped (send CLK first)");
        return;
    }
    // cpu8 has a synchronous reset: keep rst_n low for >= 10 clocks.
    uint32_t us = (uint32_t)(10000000ull / st.clk_hz);
    hold_reset();
    hw_sleep_us(us > 1000 ? us : 1000);
    st.last_uo = hw_get_uo();
    st.last_halted = false;
    st.t_go = hw_time_us();
    hw_out_clear();
    hw_reset(false);
    st.in_reset = false;
    reply("OK");
}

static void cmd_write(uint32_t addr, const char *hex)
{
    uint8_t buf[MAX_XFER];
    size_t n = strlen(hex);
    if (n % 2 != 0 || n / 2 > MAX_XFER) {
        reply("ERR need an even number of hex digits, max %d bytes", MAX_XFER);
        return;
    }
    for (size_t i = 0; i < n / 2; i++) {
        int h = hex_digit(hex[2 * i]), l = hex_digit(hex[2 * i + 1]);
        if (h < 0 || l < 0) {
            reply("ERR bad hex data");
            return;
        }
        buf[i] = (uint8_t)(h << 4 | l);
    }
    if (!check_range(addr, (uint32_t)(n / 2)))
        return;
    memcpy(hw_ram + addr, buf, n / 2);
    reply("OK %u", (unsigned)(n / 2));
}

static void cmd_read(uint32_t addr, uint32_t len)
{
    char hex[2 * MAX_XFER + 1];
    if (len == 0 || len > MAX_XFER) {
        reply("ERR length must be 1..%d", MAX_XFER);
        return;
    }
    if (!check_range(addr, len))
        return;
    for (uint32_t i = 0; i < len; i++)
        snprintf(hex + 2 * i, 3, "%02X", hw_ram[addr + i]);
    reply("OK %s", hex);
}

static void cmd_sysclk(uint32_t khz)
{
    hold_reset();
    if (!hw_set_sysclk(khz)) {
        reply("ERR cannot make %lu kHz", (unsigned long)khz);
        return;
    }
    // Re-create the project clock for the new system clock.
    if (st.clk_req != 0 && st.clk_req <= max_clock() && st.clk_req >= hw_min_clock())
        st.clk_hz = hw_set_clock(st.clk_req);
    else
        st.clk_hz = hw_set_clock(0);
    reply("OK sys=%lu clk=%lu", (unsigned long)hw_sys_hz(), (unsigned long)st.clk_hz);
}

// Send the bytes captured from OUT as "EV OUT <hex>" lines.
static void send_out_bytes(void)
{
    uint8_t buf[64];
    char hex[2 * sizeof buf + 1];
    unsigned n;
    while ((n = hw_out_read(buf, sizeof buf)) > 0) {
        for (unsigned i = 0; i < n; i++)
            snprintf(hex + 2 * i, 3, "%02X", buf[i]);
        reply("EV OUT %s", hex);
    }
}

static void execute(char *cmdline)
{
    // Output captured so far goes out before any reply, so a host that sees
    // "halted=1" in an ST reply has already received every OUT byte.
    send_out_bytes();

    char *argv[MAX_ARGS];
    int argc = 0;
    for (char *tok = strtok(cmdline, " \t"); tok; tok = strtok(NULL, " \t")) {
        if (argc == MAX_ARGS) {
            reply("ERR too many arguments");
            return;
        }
        argv[argc++] = tok;
    }
    if (argc == 0)
        return;  // empty line: no reply (lets the host resynchronise)
    for (char *p = argv[0]; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 'a' + 'A');

    static const struct { const char *name; int nargs; } cmds[] = {
        {"PING", 0}, {"HELP", 0}, {"ST", 0}, {"SEL", 1}, {"ATTACH", 0},
        {"DETACH", 0}, {"RST", 0}, {"GO", 0}, {"CLK", 1}, {"UI", 1},
        {"W", 2}, {"R", 2}, {"FILL", 3}, {"MON", 1}, {"SYSCLK", 1}, {"BOOT", 0},
    };
    const char *name = argv[0];
    int nargs = -1;
    for (unsigned i = 0; i < sizeof cmds / sizeof cmds[0]; i++)
        if (strcmp(name, cmds[i].name) == 0)
            nargs = cmds[i].nargs;
    if (nargs < 0) {
        reply("ERR unknown command '%s' (try HELP)", name);
        return;
    }
    if (argc - 1 != nargs) {
        reply("ERR %s takes %d argument(s)", name, nargs);
        return;
    }

    uint32_t a, b, c;
    if (!strcmp(name, "PING")) {
        reply("OK cpu8-loader 1");
    } else if (!strcmp(name, "HELP")) {
        cmd_help();
    } else if (!strcmp(name, "ST")) {
        cmd_status();
    } else if (!strcmp(name, "SEL")) {
        if (!arg_num(argv, 1, MAX_PROJECT, &a))
            return;
        hold_reset();
        hw_attach(false);
        hw_select_project(a);
        hw_attach(true);
        st.sel = (int)a;
        reply("OK");
    } else if (!strcmp(name, "ATTACH")) {
        hw_attach(true);
        reply("OK");
    } else if (!strcmp(name, "DETACH")) {
        hw_attach(false);
        reply("OK");
    } else if (!strcmp(name, "RST")) {
        hold_reset();
        reply("OK");
    } else if (!strcmp(name, "GO")) {
        cmd_go();
    } else if (!strcmp(name, "CLK")) {
        if (arg_num(argv, 1, UINT32_MAX, &a))
            cmd_clock(a);
    } else if (!strcmp(name, "UI")) {
        if (!arg_num(argv, 1, 255, &a))
            return;
        st.ui = (uint8_t)a;
        hw_set_ui(st.ui);
        reply("OK");
    } else if (!strcmp(name, "W")) {
        if (arg_num(argv, 1, RAM_SIZE - 1, &a))
            cmd_write(a, argv[2]);
    } else if (!strcmp(name, "R")) {
        if (arg_num(argv, 1, RAM_SIZE - 1, &a) && arg_num(argv, 2, MAX_XFER, &b))
            cmd_read(a, b);
    } else if (!strcmp(name, "FILL")) {
        if (!arg_num(argv, 1, RAM_SIZE - 1, &a) || !arg_num(argv, 2, RAM_SIZE, &b) ||
            !arg_num(argv, 3, 255, &c) || !check_range(a, b))
            return;
        memset(hw_ram + a, (int)c, b);
        reply("OK");
    } else if (!strcmp(name, "MON")) {
        if (!arg_num(argv, 1, 1, &a))
            return;
        st.monitor = a != 0;
        st.last_uo = hw_get_uo();
        st.last_halted = hw_get_halted();
        reply("OK");
    } else if (!strcmp(name, "SYSCLK")) {
        if (!arg_num(argv, 1, SYSCLK_MAX_KHZ, &a))
            return;
        if (a < SYSCLK_MIN_KHZ) {
            reply("ERR sysclk must be %d..%d kHz", SYSCLK_MIN_KHZ, SYSCLK_MAX_KHZ);
            return;
        }
        cmd_sysclk(a);
    } else if (!strcmp(name, "BOOT")) {
        reply("OK");
        hw_sleep_us(100000);  // let the reply go out
        hw_bootloader();
    }
}

void proto_input(int c)
{
    if (c == '\n' || c == '\r') {
        if (line_too_long)
            reply("ERR line too long (max %d characters)", LINE_LEN);
        else {
            line[line_len] = '\0';
            execute(line);
        }
        line_len = 0;
        line_too_long = false;
    } else if (line_len < LINE_LEN) {
        line[line_len++] = (char)c;
    } else {
        line_too_long = true;
    }
}

void proto_poll(void)
{
    send_out_bytes();  // always, and before EV HALT so the output is complete
    if (!st.monitor || st.in_reset)
        return;
    uint8_t uo = hw_get_uo();
    bool halted = hw_get_halted();
    if (uo != st.last_uo) {
        st.last_uo = uo;
        reply("EV UO %02X %lu", uo, since_go_us());
    }
    if (halted && !st.last_halted)
        reply("EV HALT %lu", since_go_us());
    st.last_halted = halted;
}
