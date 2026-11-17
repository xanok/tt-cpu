// Board header for the TT06+ demo board RP2040 (PICO_BOARD=tt_demo_rp2040).
// Same as the Raspberry Pi Pico, but the firmware runs the RP2040 at
// 200 MHz, so the flash clock is divided by 4 (50 MHz) instead of 2, which
// is safe for any QSPI flash the board may carry.
#ifndef _BOARDS_TT_DEMO_RP2040_H
#define _BOARDS_TT_DEMO_RP2040_H

#define PICO_FLASH_SPI_CLKDIV 4
#include "boards/pico.h"

#endif
