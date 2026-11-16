<!---
This file is used to generate your project datasheet. Please fill in the information below and delete any unused
sections.

You can also include images in this folder and reference them in the markdown. Each image must be less than
512 kb in size, and the combined size of all images must be less than 1 MB.
-->

## How it works

cpu8 is a tiny 8-bit accumulator CPU. It has no memory on chip. Every instruction and every data byte is read from (or written to) an external SPI RAM, in 23LC1024 style: mode 0, READ 0x03 / WRITE 0x02, 24-bit address. Instructions are 2 bytes each, and the address space is 4 KB.

`ui_in` is an 8-bit input port, read with `IN`. `uo_out` is an 8-bit output port, written with `OUT`. `uio[4]` goes high after a `HLT`. See [isa.md](isa.md) for the instruction set.

## How to test

1. Connect an SPI SRAM (for example a 23LC1024): CS# to uio[0], MOSI to uio[1], MISO to uio[2], SCK to uio[3].
2. Preload the program image (`tools/asm.py prog.asm -o prog.hex`) while the design is held in reset.
3. Release reset. The CPU starts at address 0. Watch `uo_out` and the HALTED pin.

## External hardware

An SPI SRAM with 24-bit addressing (23LC1024, or an emulation on the demo board's RP2040).
