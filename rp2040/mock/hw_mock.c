// PC mock of hw.h, so the real command parser (../src/proto.c) can be tested
// without a board. It talks over a pseudo-terminal (see mock_main.c).
//
// The "chip" is the real cpu8 instruction model (../../tools/cpu8core.h,
// the same code as cpu8sim). After reset is released it runs the program in
// RAM[0x000..0xFFF] at the project clock (its clock count follows the RTL),
// so a program takes as long as it would on the board. Every OUT goes into
// the OUT capture buffer, like the strobe interrupt on the RP2040.

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "hw.h"
#include "cpu8core.h"

static uint8_t ram[RAM_SIZE];
uint8_t *hw_ram = ram;

int mock_fd = -1;       // pty master, set by mock_main.c
int mock_verbose;

static uint32_t sys_hz = 200000000;
static uint32_t clk_hz;
static uint8_t ui, uo;
static bool attached, in_reset = true, halted;
static uint64_t t_run;  // when reset was released
static struct cpu cpu;  // the simulated chip

#define OUT_BUF 4096u
static uint8_t out_buf[OUT_BUF];
static uint32_t out_head, out_tail, out_lost;

void hw_puts(const char *s)
{
    // Like pico stdio: "\n" goes out as "\r\n". Output is dropped when the
    // other side is not reading (the master fd is non-blocking).
    for (; *s; s++) {
        if (*s == '\n' && write(mock_fd, "\r", 1) < 0)
            return;
        if (write(mock_fd, s, 1) < 0)
            return;
    }
}

uint64_t hw_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

void hw_sleep_us(uint32_t us)
{
    struct timespec ts = { us / 1000000u, (long)(us % 1000000u) * 1000 };
    nanosleep(&ts, NULL);
}

void hw_select_project(unsigned addr)
{
    if (mock_verbose)
        fprintf(stderr, "mock: select project %u\n", addr);
    hw_sleep_us(20000 + 2000 * addr);  // same timing as the real mux
}

void hw_attach(bool on) { attached = on; }

void hw_reset(bool hold)
{
    if (hold) {
        uo = 0;
        halted = false;
    } else if (in_reset) {
        t_run = hw_time_us();
        memset(&cpu, 0, sizeof cpu);  // cpu8 reset: all registers 0
    }
    in_reset = hold;
}

uint32_t hw_set_clock(uint32_t hz)
{
    if (hz == 0)
        return clk_hz = 0;
    uint32_t period = (sys_hz + hz / 2) / hz;  // whole sys clocks, like the PWM
    return clk_hz = sys_hz / period;
}

uint32_t hw_min_clock(void) { return sys_hz / (255u * 65536u) + 1; }
uint32_t hw_sys_hz(void) { return sys_hz; }

bool hw_set_sysclk(uint32_t khz)
{
    sys_hz = khz * 1000u;
    clk_hz = 0;
    return true;
}

void hw_set_ui(uint8_t v) { ui = v; }

void mock_cpu_update(void)
{
    if (in_reset || halted || clk_hz == 0 || !attached)
        return;
    // Run until the CPU's clock count catches up with wall-clock time.
    uint64_t due = (hw_time_us() - t_run) * clk_hz / 1000000u;
    if (cpu.clocks >= due)
        return;
    memcpy(cpu.mem, ram, CPU8_MEM_SIZE);
    cpu.in = ui;
    while (!cpu.halted && cpu.clocks < due) {
        int out = step(&cpu);
        if (out >= 0) {
            uo = (uint8_t)out;
            if (out_head - out_tail < OUT_BUF)
                out_buf[out_head++ & (OUT_BUF - 1)] = (uint8_t)out;
            else
                out_lost++;
        }
    }
    memcpy(ram, cpu.mem, CPU8_MEM_SIZE);
    halted = cpu.halted;
}

unsigned hw_out_read(uint8_t *buf, unsigned max)
{
    mock_cpu_update();
    unsigned n = 0;
    while (n < max && out_tail != out_head)
        buf[n++] = out_buf[out_tail++ & (OUT_BUF - 1)];
    return n;
}

void hw_out_clear(void)
{
    out_tail = out_head;
    out_lost = 0;
}

uint32_t hw_out_lost(void) { return out_lost; }

uint8_t hw_get_uo(void)
{
    mock_cpu_update();
    return uo;
}

bool hw_get_halted(void)
{
    mock_cpu_update();
    return halted;
}

void hw_bootloader(void) { fprintf(stderr, "mock: BOOT (ignored)\n"); }
