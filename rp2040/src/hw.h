// Hardware interface used by the command parser (proto.c).
// Implemented by hw_pico.c on the RP2040 and by mock/hw_mock.c on a PC.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RAM_SIZE 65536u

extern uint8_t *hw_ram;               // the emulated 64 KB SPI RAM

void     hw_puts(const char *s);      // send text to the host
uint64_t hw_time_us(void);            // monotonic microseconds
void     hw_sleep_us(uint32_t us);

void     hw_select_project(unsigned addr); // drive the TT project mux
void     hw_attach(bool on);          // drive ui_in and MISO (true) or float them
void     hw_reset(bool hold);         // true: rst_n low; false: release
uint32_t hw_set_clock(uint32_t hz);   // 0 stops; returns the real frequency
uint32_t hw_min_clock(void);          // lowest clock hw_set_clock can make
uint32_t hw_sys_hz(void);
bool     hw_set_sysclk(uint32_t khz); // false if not possible
void     hw_set_ui(uint8_t v);
uint8_t  hw_get_uo(void);
bool     hw_get_halted(void);
void     hw_bootloader(void);         // reboot into the UF2 bootloader
