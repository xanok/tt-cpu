// Loopback test of the SPI RAM emulator on a real Pico 2 (RP2350), without
// the chip. See emutest/README.md, "On a real Pico 2".
//
// core1 runs the emulator exactly as in the loader firmware (spi-ram-emu,
// pins GP16..GP19). core0 plays the chip: a state machine of the third PIO
// block drives CS, SCK and MOSI on GP2..GP4 from a waveform recorded from the
// chip's Verilog (trace.cpp), one chip clock per RATIO system clocks, and
// reads MISO on GP5 at the end of every chip clock. Four jumper wires join
// the two sets of pins. loop.py sends the waveform over USB, reads the
// samples back and checks them, and the RAM, against the Verilog.
//
// Commands (one per line, numbers in hex unless noted):
//   P <hex bytes>        clear the 64 KB RAM, load a program at 0
//   D <word> <hex words> drive buffer from word index <word>
//   RUN <nd> <ns> <ratio>   play nd drive words, collect ns sample words
//                        (decimal); answers "OK <us> <stall>"
//   S <word> <n>         n sample words (n <= 256)
//   M <addr> <len>       RAM bytes (len <= 1024)
//   Z                    count of non-zero bytes above 0x1000
//   ID / BOOT
// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"

#include "pinmap.h"
#include "sram.h"
#include "loop.pio.h"

#define T_CS    2   // GP2 -> GP16 (CS_N)
#define T_SCK   3   // GP3 -> GP19 (SCK)
#define T_MOSI  4   // GP4 -> GP17 (MOSI)
#define T_MISO  5   // GP5 <- GP18 (MISO)
_Static_assert(T_SCK == T_CS + 1 && T_MOSI == T_CS + 2, "out pins must be in a row");

#define ND_MAX  26000          // drive words: 10 chip clocks each
#define NS_MAX  8200           // sample words: 32 chip clocks each
static uint32_t drive[ND_MAX];
static uint32_t samples[NS_MAX];
static uint8_t *ram;

static PIO pio = pio2;
static uint sm, offset;
static int ch_tx, ch_rx;

#ifdef LOOP_STOCK
#define VARIANT "stock"
#else
#define VARIANT "cpu8"
#endif

static void idle_pins(void)
{
    pio_sm_set_pins_with_mask(pio, sm, 1u << T_CS, 7u << T_CS);   // CS high, SCK and MOSI low
}

static void tester_init(void)
{
    sm = pio_claim_unused_sm(pio, true);
    offset = pio_add_program(pio, &loop_drive_program);
    pio_sm_config c = loop_drive_program_get_default_config(offset);
    sm_config_set_out_pins(&c, T_CS, 3);
    sm_config_set_in_pins(&c, T_MISO);
    sm_config_set_out_shift(&c, true, true, 30);   // 10 chip clocks per word
    sm_config_set_in_shift(&c, true, true, 32);
    pio_sm_init(pio, sm, offset, &c);
    idle_pins();
    pio_sm_set_pindirs_with_mask(pio, sm, 7u << T_CS, 7u << T_CS);
    for (int p = T_CS; p < T_CS + 3; p++) pio_gpio_init(pio, p);
    gpio_init(T_MISO);
    gpio_set_dir(T_MISO, GPIO_IN);
    gpio_disable_pulls(T_MISO);
    ch_tx = dma_claim_unused_channel(true);
    ch_rx = dma_claim_unused_channel(true);
}

// play nd drive words, collect ns sample words. Returns the run time in us,
// or -1 on a timeout; *stall is set when the drive FIFO ran dry while the
// samples were still being taken (the waveform was then not played on time).
static long run(int nd, int ns, int ratio, int *stall)
{
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_put(pio, sm, (uint32_t)(ratio - 4));
    pio_sm_exec(pio, sm, pio_encode_pull(false, true));
    pio_sm_exec(pio, sm, pio_encode_mov(pio_y, pio_osr));
    pio_sm_exec(pio, sm, pio_encode_out(pio_null, 32));   // empty OSR: the first out pulls
    pio_sm_clear_fifos(pio, sm);
    pio_sm_exec(pio, sm, pio_encode_jmp(offset));

    dma_channel_config c = dma_channel_get_default_config(ch_rx);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));
    dma_channel_configure(ch_rx, &c, samples, &pio->rxf[sm], (uint)ns, true);
    c = dma_channel_get_default_config(ch_tx);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
    dma_channel_configure(ch_tx, &c, &pio->txf[sm], drive, (uint)nd, true);
    while (!pio_sm_is_tx_fifo_full(pio, sm)) tight_loop_contents();

    pio->fdebug = 0xffffffffu;                            // clear the stall flags
    absolute_time_t t0 = get_absolute_time();
    pio_sm_set_enabled(pio, sm, true);
    long us = 0;
    while (dma_channel_is_busy(ch_rx)) {
        us = (long)absolute_time_diff_us(t0, get_absolute_time());
        if (us > 2000000) break;
    }
    int timeout = dma_channel_is_busy(ch_rx);
    *stall = (pio->fdebug >> (PIO_FDEBUG_TXSTALL_LSB + sm)) & 1;
    pio_sm_set_enabled(pio, sm, false);
    dma_channel_abort(ch_tx);
    dma_channel_abort(ch_rx);
    idle_pins();
    return timeout ? -1 : (long)absolute_time_diff_us(t0, get_absolute_time());
}

static char line[2300];

static int read_line(void)
{
    int n = 0;
    for (;;) {
        int c = getchar_timeout_us(1000000);
        if (c == PICO_ERROR_TIMEOUT) continue;
        if (c == '\r') continue;
        if (c == '\n') { line[n] = 0; return n; }
        if (n < (int)sizeof line - 1) line[n++] = (char)c;
    }
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int main(void)
{
    // CS high before anything else, so the emulator starts idle
    gpio_init(T_CS);
    gpio_set_dir(T_CS, GPIO_OUT);
    gpio_put(T_CS, 1);
    ram = setup_simulated_sram();         // claims its PIO SMs and DMA channels 0..2 first
    sram_drive_miso(true);
    tester_init();
    stdio_init_all();
    stdio_set_translate_crlf(&stdio_usb, false);
    memset(ram, 0, 65536);

    for (;;) {
        read_line();
        char *a = line;
        if (!strncmp(a, "ID", 2)) {
            printf("OK cpu8 loopback %s sys=%lu\n", VARIANT, (unsigned long)clock_get_hz(clk_sys));
        } else if (a[0] == 'P' && a[1] == ' ') {
            memset(ram, 0, 65536);
            int n = 0;
            for (char *p = a + 2; p[0] && p[1] && n < 4096; p += 2) ram[n++] = (uint8_t)(hexval(p[0]) << 4 | hexval(p[1]));
            printf("OK %d\n", n);
        } else if (a[0] == 'D' && a[1] == ' ') {
            char *p;
            long w = strtol(a + 2, &p, 16);
            while (*p == ' ') p++;
            int n = 0;
            while (p[0] && w < ND_MAX) {
                uint32_t v = 0;
                for (int k = 0; k < 8; k++) v = v << 4 | (uint32_t)hexval(p[k]);
                drive[w++] = v;
                p += 8;
                n++;
            }
            printf("OK %d\n", n);
        } else if (!strncmp(a, "RUN ", 4)) {
            int nd = 0, ns = 0, ratio = 0, stall = 0;
            sscanf(a + 4, "%d %d %d", &nd, &ns, &ratio);
            if (nd < 1 || nd > ND_MAX || ns < 1 || ns > NS_MAX || ratio < 5 || ratio > 1000) { printf("ERR args\n"); continue; }
            long us = run(nd, ns, ratio, &stall);
            if (us < 0) printf("ERR timeout\n");
            else printf("OK %ld %d\n", us, stall);
        } else if (a[0] == 'S' && a[1] == ' ') {
            unsigned w = 0, n = 0;
            sscanf(a + 2, "%x %x", &w, &n);
            if (n > 256 || w + n > NS_MAX) { printf("ERR args\n"); continue; }
            printf("OK ");
            for (unsigned i = 0; i < n; i++) printf("%08lx", (unsigned long)samples[w + i]);
            printf("\n");
        } else if (a[0] == 'M' && a[1] == ' ') {
            unsigned addr = 0, len = 0;
            sscanf(a + 2, "%x %x", &addr, &len);
            if (len > 1024 || addr + len > 65536) { printf("ERR args\n"); continue; }
            printf("OK ");
            for (unsigned i = 0; i < len; i++) printf("%02x", ram[addr + i]);
            printf("\n");
        } else if (a[0] == 'Z') {
            int n = 0;
            for (int i = 4096; i < 65536; i++) n += ram[i] != 0;
            printf("OK %d\n", n);
        } else if (!strncmp(a, "BOOT", 4)) {
            printf("OK\n");
            stdio_flush();
            sleep_ms(100);
            reset_usb_boot(0, 0);
        } else if (a[0]) {
            printf("ERR unknown\n");
        }
    }
}
