// PC mock of hw.h, so the real command parser (../src/proto.c) can be tested
// without a board. It talks over a pseudo-terminal (see mock_main.c).
//
// Instead of cpu8 it has a "fake CPU" with a fixed script, enough to test
// the loader: after reset is released (with the clock running) it shows
// 1, 2, 3, 4, 5 on uo_out, one step every 20 ms, then stores
//   RAM[0x0FFE] = ui_in ^ 0xFF
//   RAM[0x0FFF] = 8-bit sum of RAM[0x000..0xEFF]   (the loaded program)
// and raises HALTED at 100 ms.

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "hw.h"

static uint8_t ram[RAM_SIZE];
uint8_t *hw_ram = ram;

int mock_fd = -1;       // pty master, set by mock_main.c
int mock_verbose;

static uint32_t sys_hz = 200000000;
static uint32_t clk_hz;
static uint8_t ui, uo;
static bool attached, in_reset = true, halted;
static uint64_t t_run;  // when reset was released

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
    uint64_t ms = (hw_time_us() - t_run) / 1000u;
    uo = (uint8_t)(ms / 20 < 5 ? ms / 20 + 1 : 5);
    if (ms >= 100) {
        uint8_t sum = 0;
        for (unsigned i = 0; i < 0xF00; i++)
            sum = (uint8_t)(sum + ram[i]);
        ram[0x0FFE] = (uint8_t)(ui ^ 0xFF);
        ram[0x0FFF] = sum;
        halted = true;
    }
}

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
