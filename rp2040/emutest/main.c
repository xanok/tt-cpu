// Test firmware for the RP2040 emulator (rp2040js), see emutest/README.md.
// It runs ONLY the SPI RAM emulator: the unmodified spi-ram-emu/sram.c and
// sram.pio of the real firmware. rp2040js has no second core, so the
// emulator's service loop (normally on core1) runs on core0 here: this file
// provides multicore_launch_core1() and calls the loop itself.

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "pinmap.h"
#include "sram.h"

static void (*core1_entry)(void);

void multicore_launch_core1(void (*entry)(void))
{
    core1_entry = entry;
}

int main(void)
{
    setup_simulated_sram();
    sram_drive_miso(true);        // what ATTACH / SEL does in the real firmware
    gpio_init(PIN_READY);
    gpio_set_dir(PIN_READY, GPIO_OUT);
    gpio_put(PIN_READY, 1);       // tells the test harness the emulator is armed
    core1_entry();                // never returns
    return 0;
}
