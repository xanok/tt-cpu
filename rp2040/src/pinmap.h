// GPIO map of the Tiny Tapeout demo board. Change pins here only.
//
// CMake picks the board (-DTT_BOARD=rp2350, rp2040 or pico2):
//
// TT_BOARD_RP2350 (default): the current "TT DB v3" / ETR demo board with an
//   RP2350B, the one the GF180 shuttles ship with. Source: class
//   GPIOMapTTDBv3 in tt-micropython-firmware,
//   src/ttboard/pins/gpio_map_dbv3.py (commit 15ba5ce).
//
// TT_BOARD_RP2040: the TT06+ demo board with an RP2040 (TT06, TT07, TT08 and
//   the later RP2040 shuttles). Source: class GPIOMapTT06 in the last RP2040
//   version of that firmware. NOT the TT04/TT05 board, which shares the mux
//   control lines with uo_out[1..3].
//
// TT_BOARD_PICO2: bench test with a Raspberry Pi Pico 2 instead of a demo
//   board (see below and docs/bench.md).
//
// On all of them uio[0..7] are consecutive GPIOs below 32, so the SPI RAM
// emulator's PIO programs (CS = MOSI - 1, SCK = MOSI + 2) fit unchanged.
#pragma once

#if defined(TT_BOARD_RP2350)

#define PIN_PROJ_CLK        16  // project clk (PWM)
#define PIN_PROJ_RST_N      14  // project rst_n (board has a pull-up and a button)
#define PIN_CTRL_ENA        0   // project mux control (select a project by address)
#define PIN_CTRL_SEL_RST_N  1
#define PIN_CTRL_SEL_INC    2
#define TT07_CB_FIX         0

#define PINS_UI  { 17, 18, 19, 20, 21, 22, 23, 24 }
#define PINS_UO  { 33, 34, 35, 36, 37, 38, 39, 40 }   // in the high GPIO bank
// uo_out from a 64-bit gpio_get_all64() value
#define UO_FROM_GPIO(all)  ((uint8_t)((all) >> 33))
// uo_out read directly, for the OUT strobe interrupt: GPIO 32..47 are bits
// 0..15 of the high input register
#define UO_READ_FAST()     ((uint8_t)(sio_hw->gpio_hi_in >> 1))
#define GPIO_GET_ALL()     gpio_get_all64()
typedef uint64_t gpio_all_t;

#define PIN_UIO(n)          (25 + (n))

#elif defined(TT_BOARD_PICO2)

// Bench test, no Tiny Tapeout chip: a Raspberry Pi Pico 2 (RP2350A) wired
// with jumper wires to a Tang Nano 20K that holds the chip's Verilog
// (fpga/, "make chip"). Wiring table: docs/bench.md. There is no project
// mux, so SEL only attaches the pins.
#define TT_NO_MUX           1
#define PIN_PROJ_CLK        26  // GP26 (PWM)
#define PIN_PROJ_RST_N      27  // GP27; no board pull-up here, the pin's own pull-up is used
#define TT07_CB_FIX         0

#define PINS_UI  { 0, 1, 2, 3, 4, 5, 6, 7 }            // GP0..GP7 (the FPGA build wires ui_in[2:0] only)
#define PINS_UO  { 8, 9, 10, 11, 12, 13, 14, 15 }      // GP8..GP15
#define UO_FROM_GPIO(all)  ((uint8_t)((all) >> 8))
#define UO_READ_FAST()     UO_FROM_GPIO(gpio_get_all())
#define GPIO_GET_ALL()     gpio_get_all()
typedef uint32_t gpio_all_t;

#define PIN_UIO(n)          (16 + (n))                  // GP16..GP22 = uio0..uio6

#elif defined(TT_BOARD_RP2040)

#define PIN_PROJ_CLK        0   // project clk (PWM slice 0, channel A)
#define PIN_PROJ_RST_N      1   // project rst_n (board has a pull-up and a button)
#define PIN_CTRL_SEL_RST_N  2   // project mux control (select a project by address)
#define PIN_CTRL_SEL_INC    3
#define PIN_CTRL_ENA        4
// The TT07 carrier board swapped ctrl_sel_rst_n and ctrl_sel_inc. Set this
// to 1 only for a TT07 chip.
#define TT07_CB_FIX         0

// ui_in[0..7] and uo_out[0..7] are not on consecutive GPIOs.
#define PINS_UI  { 9, 10, 11, 12, 17, 18, 19, 20 }
#define PINS_UO  { 5,  6,  7,  8, 13, 14, 15, 16 }
// uo_out from a gpio_get_all() value, without a table lookup
#define UO_FROM_GPIO(all)  ((uint8_t)((((all) >> 5) & 0x0Fu) | (((all) >> 9) & 0xF0u)))
#define UO_READ_FAST()     UO_FROM_GPIO(gpio_get_all())
#define GPIO_GET_ALL()     gpio_get_all()
typedef uint32_t gpio_all_t;

#define PIN_UIO(n)          (21 + (n))

#else
#error "define TT_BOARD_RP2350, TT_BOARD_RP2040 or TT_BOARD_PICO2 (CMake: -DTT_BOARD=rp2350|rp2040|pico2)"
#endif

// cpu8 use of the uio pins (see ../../src/project.v), same on both boards
#define PIN_SPI_CS_N        PIN_UIO(0)  // chip -> RP2
#define PIN_SPI_MOSI        PIN_UIO(1)  // chip -> RP2
#define PIN_SPI_MISO        PIN_UIO(2)  // RP2 -> chip
#define PIN_SPI_SCK         PIN_UIO(3)  // chip -> RP2
#define PIN_HALTED          PIN_UIO(4)  // chip -> RP2
#define PIN_OUT_STROBE      PIN_UIO(5)  // chip -> RP2, toggles on every OUT
#define PIN_MISO_RISE       PIN_UIO(6)  // RP2 -> chip, strap: MISO sample edge
