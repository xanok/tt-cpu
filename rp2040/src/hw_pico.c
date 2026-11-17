// RP2040 / RP2350 side of hw.h: TT demo board pins, project clock (PWM) and
// mux. The board is chosen in pinmap.h.

#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "hardware/pwm.h"
#include "hardware/vreg.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include "hw.h"
#include "pinmap.h"
#include "sram.h"

uint8_t *hw_ram;

static const uint8_t pins_ui[8] = PINS_UI;
static const uint8_t pins_uo[8] = PINS_UO;
static uint32_t ui_mask;

#if TT07_CB_FIX
#define PIN_INC  PIN_CTRL_SEL_RST_N
#define PIN_NCRST PIN_CTRL_SEL_INC
#else
#define PIN_INC  PIN_CTRL_SEL_INC
#define PIN_NCRST PIN_CTRL_SEL_RST_N
#endif

static void out_pin(unsigned pin, bool value)
{
    gpio_init(pin);
    gpio_put(pin, value);
    gpio_set_dir(pin, GPIO_OUT);
}

static void in_pin(unsigned pin)
{
    gpio_init(pin);
    gpio_set_pulls(pin, false, true);  // pull-down, like the TT firmware
}

// ---- OUT capture ----
// cpu8 toggles OUT_STROBE on every OUT. The pin interrupt (both edges, top
// priority so USB handling cannot delay it) reads uo_out into a ring buffer.
// An OUT takes at least 88 project clocks (8.8 us at 10 MHz), and uo_out
// stays stable until the next one, so there is plenty of time.
#define OUT_BUF 4096u  // power of two
static volatile uint8_t out_buf[OUT_BUF];
static volatile uint32_t out_head, out_tail, out_lost;

// Raw IO_IRQ_BANK0 handler, entirely in RAM (the SDK's GPIO callback
// dispatcher runs from flash, where a cache miss costs microseconds).
static void __not_in_flash_func(strobe_irq)(void)
{
    // acknowledge both edge events of the strobe pin (write-1-to-clear)
    io_bank0_hw->intr[PIN_OUT_STROBE / 8] = 0xCu << (4 * (PIN_OUT_STROBE % 8));
    uint8_t v = UO_READ_FAST();
    uint32_t h = out_head;
    if (h - out_tail < OUT_BUF) {
        out_buf[h & (OUT_BUF - 1)] = v;
        out_head = h + 1;
    } else {
        out_lost++;
    }
}

unsigned hw_out_read(uint8_t *buf, unsigned max)
{
    unsigned n = 0;
    uint32_t t = out_tail;
    while (n < max && t != out_head)
        buf[n++] = out_buf[t++ & (OUT_BUF - 1)];
    out_tail = t;
    return n;
}

void hw_out_clear(void)
{
    uint32_t irq = save_and_disable_interrupts();
    out_tail = out_head;
    out_lost = 0;
    restore_interrupts(irq);
}

uint32_t hw_out_lost(void) { return out_lost; }

void hw_init(void)
{
    // Mux control: nothing selected until SEL.
    out_pin(PIN_CTRL_ENA, 0);
    out_pin(PIN_INC, 0);
    out_pin(PIN_NCRST, 1);

    out_pin(PIN_PROJ_RST_N, 0);  // project held in reset
    gpio_pull_up(PIN_PROJ_RST_N);  // helps the board pull-up once released
    out_pin(PIN_PROJ_CLK, 0);    // clock stopped

    for (int i = 0; i < 8; i++) {
        in_pin(pins_ui[i]);       // float until ATTACH/SEL
        in_pin(pins_uo[i]);
        ui_mask |= 1u << pins_ui[i];
    }
    for (int i = 4; i < 8; i++)   // HALTED, OUT_STROBE, strap (until attached), unused
        in_pin(PIN_UIO(i));
    gpio_put(PIN_MISO_RISE, 0);
    // uio0..3 (SPI) are set up by setup_simulated_sram().

    for (int i = 0; i < 8; i++)  // UO_FROM_GPIO must agree with PINS_UO
        if (UO_FROM_GPIO((gpio_all_t)1 << pins_uo[i]) != (1u << i))
            panic("pinmap.h: UO_FROM_GPIO does not match PINS_UO");
    gpio_set_irq_enabled(PIN_OUT_STROBE, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    irq_set_exclusive_handler(IO_IRQ_BANK0, strobe_irq);
    irq_set_priority(IO_IRQ_BANK0, 0);  // highest: USB cannot delay it
    irq_set_enabled(IO_IRQ_BANK0, true);
}

void hw_puts(const char *s)
{
    fputs(s, stdout);
    fflush(stdout);
}

uint64_t hw_time_us(void) { return time_us_64(); }
void hw_sleep_us(uint32_t us) { sleep_us(us); }

// Same sequence as ProjectMux.reset_and_clock_mux() in tt-micropython-firmware.
void hw_select_project(unsigned addr)
{
    gpio_put(PIN_INC, 0);
    gpio_put(PIN_NCRST, 0);
    gpio_put(PIN_CTRL_ENA, 0);
    sleep_ms(10);
    gpio_put(PIN_NCRST, 1);
    sleep_ms(10);
    for (unsigned i = 0; i < addr; i++) {
        gpio_put(PIN_INC, 1);
        sleep_ms(1);
        gpio_put(PIN_INC, 0);
        sleep_ms(1);
    }
    gpio_put(PIN_CTRL_ENA, 1);
}

void hw_attach(bool on)
{
    for (int i = 0; i < 8; i++) {
        if (on) {
            gpio_set_dir(pins_ui[i], GPIO_OUT);
        } else {
            gpio_set_dir(pins_ui[i], GPIO_IN);
        }
    }
    gpio_set_dir(PIN_MISO_RISE, on ? GPIO_OUT : GPIO_IN);
    sram_drive_miso(on);
}

// Like DemoBoard.reset_project(): drive low to reset; to release, drive high
// briefly and then let the board pull-up (and the reset button) take over.
void hw_reset(bool hold)
{
    if (hold) {
        gpio_put(PIN_PROJ_RST_N, 0);
        gpio_set_dir(PIN_PROJ_RST_N, GPIO_OUT);
    } else {
        gpio_put(PIN_PROJ_RST_N, 1);
        sleep_us(10);
        gpio_set_dir(PIN_PROJ_RST_N, GPIO_IN);
    }
}

// PWM: f = sys / (div * (top + 1)), integer divider 1..255, top < 65536.
uint32_t hw_set_clock(uint32_t hz)
{
    unsigned slice = pwm_gpio_to_slice_num(PIN_PROJ_CLK);
    unsigned chan = pwm_gpio_to_channel(PIN_PROJ_CLK);
    pwm_set_enabled(slice, false);
    if (hz == 0) {
        gpio_init(PIN_PROJ_CLK);
        gpio_put(PIN_PROJ_CLK, 0);
        gpio_set_dir(PIN_PROJ_CLK, GPIO_OUT);
        return 0;
    }
    uint32_t sys = clock_get_hz(clk_sys);
    uint32_t period = (sys + hz / 2) / hz;  // in sys clocks
    uint32_t div = (period + 65535) / 65536;
    if (div < 1) div = 1;
    if (div > 255) div = 255;
    uint32_t top = (sys / div + hz / 2) / hz;
    if (top < 2) top = 2;
    if (top > 65536) top = 65536;
    top -= 1;

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&cfg, div);
    pwm_config_set_wrap(&cfg, (uint16_t)top);
    pwm_init(slice, &cfg, false);
    pwm_set_chan_level(slice, chan, (top + 1) / 2);
    gpio_set_function(PIN_PROJ_CLK, GPIO_FUNC_PWM);
    pwm_set_enabled(slice, true);
    return sys / (div * (top + 1));
}

uint32_t hw_min_clock(void)
{
    uint32_t sys = clock_get_hz(clk_sys);
    return sys / (255u * 65536u) + 1;
}

uint32_t hw_sys_hz(void) { return clock_get_hz(clk_sys); }

bool hw_set_sysclk(uint32_t khz)
{
    uint vco, pd1, pd2;
    if (!check_sys_clock_khz(khz, &vco, &pd1, &pd2))
        return false;
    // Raise the voltage before speeding up, lower it after slowing down.
#if PICO_RP2040
    // 1.15 V is what the SDK uses for 200 MHz; above that is overclocking.
    enum vreg_voltage v = khz > 200000 ? VREG_VOLTAGE_1_20 : VREG_VOLTAGE_1_15;
#else
    // RP2350: 1.10 V is the default and covers the rated 150 MHz; anything
    // faster is overclocking and gets more voltage.
    enum vreg_voltage v = khz > 200000 ? VREG_VOLTAGE_1_20 :
                          khz > 150000 ? VREG_VOLTAGE_1_15 : VREG_VOLTAGE_1_10;
#endif
    if (khz * 1000ull > clock_get_hz(clk_sys)) {
        vreg_set_voltage(v);
        sleep_ms(2);
    }
    if (!set_sys_clock_khz(khz, false))
        return false;
    vreg_set_voltage(v);
    return true;
}

void hw_set_miso_rise(bool rise) { gpio_put(PIN_MISO_RISE, rise); }

void hw_set_ui(uint8_t v)
{
    uint32_t bits = 0;
    for (int i = 0; i < 8; i++)
        if (v & (1u << i))
            bits |= 1u << pins_ui[i];
    gpio_put_masked(ui_mask, bits);
}

uint8_t hw_get_uo(void)
{
    return UO_FROM_GPIO(GPIO_GET_ALL());
}

bool hw_get_halted(void) { return gpio_get(PIN_HALTED); }

void hw_bootloader(void) { reset_usb_boot(0, 0); }
