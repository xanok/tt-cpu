<!---
This file is used to generate your project datasheet. Please fill in the information below and delete any unused
sections.

You can also include images in this folder and reference them in the markdown. Each image must be less than
512 kb in size, and the combined size of all images must be less than 1 MB.
-->

## How it works

cpu8 is a tiny 8-bit accumulator CPU. It has no memory on chip. Every instruction and every data byte is read from (or written to) an external SPI RAM, in 23LC512 style: mode 0, READ 0x03 / WRITE 0x02, 16-bit address. Instructions are 2 bytes each, and the address space is 4 KB.

`ui_in` is an 8-bit input port, read with `IN`. `uo_out` is an 8-bit output port, written with `OUT`. `uio[4]` goes high after a `HLT`. See [isa.md](isa.md) for the instruction set.

## How to test

1. On the demo board, run [spi-ram-emu](https://github.com/MichaelBell/spi-ram-emu) on the RP2040, wired to uio[0] CS#, uio[1] MOSI, uio[2] MISO and uio[3] SCK.
2. While the design is held in reset, copy the program image (`tools/asm.py prog.asm -o prog.hex`) into the emulated RAM, starting at address 0.
3. Select the project, set the clock to 10 MHz (SCK = 5 MHz, well under the emulator's 12.5 MHz READ limit), and release reset. Watch `uo_out` on the 7-segment display, and the HALTED pin.

## External hardware

None beyond the demo board: the RP2040 emulates the SPI RAM. A real 23LC512 (16-bit address) also works.
