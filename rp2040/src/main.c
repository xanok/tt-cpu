// cpu8 loader firmware for the Tiny Tapeout demo board (RP2040).
// core1: spi-ram-emu, the 64 KB SPI RAM that cpu8 runs from.
// core0: USB-CDC command handler (proto.c).

#include "pico/stdlib.h"

#include "hw.h"
#include "proto.h"
#include "sram.h"

void hw_init(void);

int main(void)
{
    // Claim the PIO state machines and DMA channels spi-ram-emu hardcodes
    // before anything else, as its README advises.
    hw_ram = setup_simulated_sram();
    for (unsigned i = 0; i < RAM_SIZE; i++)
        hw_ram[i] = 0;
    hw_init();
    stdio_init_all();

    for (;;) {
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT)
            proto_input(c);
        proto_poll();
    }
}
