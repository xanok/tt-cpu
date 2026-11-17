// Copyright 2023 (c) Michael Bell
// The BSD 3 clause license applies
#pragma once

#include <stdbool.h>
#include <stdint.h>

// cpu8 change: the GPIOs come from ../src/pinmap.h. Upstream needs
// SCK = MOSI + 1 and CS = MOSI + 2; this copy takes any layout that matches
// the SRAM_SCK_OFS / SRAM_CS_OFS defines at the top of sram.pio
// (checked in sram.c).
#include "pinmap.h"
#define SIM_SRAM_SPI_MOSI PIN_SPI_MOSI
#define SIM_SRAM_SPI_SCK  PIN_SPI_SCK
#define SIM_SRAM_SPI_CS   PIN_SPI_CS_N
#define SIM_SRAM_SPI_MISO PIN_SPI_MISO

// The PIO SMs and DMA channels are hardcoded as using dynamic
// allocation and then reading the values from memory is slightly slower.

// Configuration: PIO and SMs to use
// A different PIO must be used for each of read and write 
// as the total program size is too large to fit in one PIO
#define SIM_SRAM_pio_read_sm   1
#define SIM_SRAM_pio_read      pio1
#define SIM_SRAM_pio_write_sm  1
#define SIM_SRAM_pio_write     pio0

// Configuration: DMA channels
#define SIM_SRAM_rx_channel    0
#define SIM_SRAM_tx_channel    1
#define SIM_SRAM_tx_channel2   2

// Setup the simulated SRAM and launch core1 to service the commands.
//
// It is best to call this before other initialization, so that
// the hardcoded DMA channels and SMs are claimed before other resources
// are claimed.
uint8_t* setup_simulated_sram(void);

// cpu8 addition: setup_simulated_sram() leaves MISO undriven (plain input).
// Call with true to hand the MISO pad to the PIO, false to float it again.
void sram_drive_miso(bool on);