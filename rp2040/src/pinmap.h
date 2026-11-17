// RP2040 GPIO map of the Tiny Tapeout demo board. Change pins here only.
//
// Board: TT06+ demo board (RP2040). It was used with TT06, TT07, TT08 and the
// later RP2040 shuttles. Source: tt-micropython-firmware, class GPIOMapTT06
// in src/ttboard/pins/gpio_map.py (last RP2040 version, the parent of commit
// 37cf3e8 "Exclusive RP2350/ETR DB support: previous RP2040 support
// withdrawn", 2026-01-27).
//
// NOT for the TT04/TT05 demo board: that one shares the mux control lines
// with uo_out[1..3] through an on-board analog mux (GPIOMapTT04).
//
// NOT for the newer "TT DB v3" / ETR demo board: it has an RP2350B, not an
// RP2040. Its map (GPIOMapTTDBv3) is listed at the bottom for reference.
#pragma once

#define PIN_PROJ_CLK        0   // project clk (PWM slice 0, channel A)
#define PIN_PROJ_RST_N      1   // project rst_n (board has a pull-up and a button)

// Project mux control (select a project by address)
#define PIN_CTRL_SEL_RST_N  2
#define PIN_CTRL_SEL_INC    3
#define PIN_CTRL_ENA        4

// The TT07 carrier board swapped ctrl_sel_rst_n and ctrl_sel_inc. Set this
// to 1 only for a TT07 chip.
#define TT07_CB_FIX         0

// ui_in[0..7] and uo_out[0..7] are not on consecutive GPIOs.
#define PINS_UI  { 9, 10, 11, 12, 17, 18, 19, 20 }
#define PINS_UO  { 5,  6,  7,  8, 13, 14, 15, 16 }
// uo_out from a gpio_get_all() value, without a table lookup (used in the
// OUT strobe interrupt). Must match PINS_UO; hw_pico.c checks it at start.
#define UO_FROM_GPIO(all)  ((uint8_t)((((all) >> 5) & 0x0Fu) | (((all) >> 9) & 0xF0u)))

// uio[n] = GPIO 21 + n
#define PIN_UIO(n)          (21 + (n))

// cpu8 use of the uio pins (see ../../src/project.v)
#define PIN_SPI_CS_N        PIN_UIO(0)  // chip -> RP2040
#define PIN_SPI_MOSI        PIN_UIO(1)  // chip -> RP2040
#define PIN_SPI_MISO        PIN_UIO(2)  // RP2040 -> chip
#define PIN_SPI_SCK         PIN_UIO(3)  // chip -> RP2040
#define PIN_HALTED          PIN_UIO(4)  // chip -> RP2040
#define PIN_OUT_STROBE      PIN_UIO(5)  // chip -> RP2040, toggles on every OUT

// For reference, TT DB v3 (RP2350B, GPIOMapTTDBv3, current firmware):
//   clk 16, rst_n 14, ctrl_ena 0, ctrl_sel_rst_n 1, ctrl_sel_inc 2,
//   ui_in 17..24, uio 25..32, uo_out 33..40, manual clock button 15.
// The relative uio layout is the same (CS=uio0 ... SCK=uio3), so the
// modified spi-ram-emu PIO programs would still fit, but the RP2350 build
// also needs PICO_PLATFORM=rp2350, a different memory map for the emulated
// RAM (0x20030000 is ordinary striped SRAM there) and reading uo_out from
// the high GPIO bank (pins >= 32). That port is not done.
