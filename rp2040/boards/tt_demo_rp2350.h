// Board header for the current Tiny Tapeout demo board ("TT DB v3" / ETR),
// which carries an RP2350B (PICO_BOARD=tt_demo_rp2350). It is the board the
// GF180 shuttles ship with.
#ifndef _BOARDS_TT_DEMO_RP2350_H
#define _BOARDS_TT_DEMO_RP2350_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

#define TT_DEMO_RP2350
#define PICO_RP2350A 0              // RP2350B: 48 GPIOs

#define PICO_DEFAULT_LED_PIN 11

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 4     // safe for any QSPI flash, also above 150 MHz
#endif
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)  // the firmware needs far less
#endif

#endif
