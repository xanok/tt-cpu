# cpu8 input and output

How a cpu8 program talks to the outside world: the pins, text output, keyboard input, and how each of them works in the simulator, on the Tiny Tapeout demo board and on the Tang Nano 20K.

- [Overview](#overview)
- [Memory map](#memory-map)
- [Output: `OUT`](#output-out)
- [Pin input: `IN`](#pin-input-in)
- [Keyboard input: the mailbox](#keyboard-input-the-mailbox)
- [Running a program with input and output](#running-a-program-with-input-and-output)
- [Writing an interactive program](#writing-an-interactive-program)
- [How each platform implements it](#how-each-platform-implements-it)
- [Tests](#tests)
- [Limits](#limits)

## Overview

| Channel | Direction | In the program | What it is | In the chip? |
|---------|-----------|----------------|------------|--------------|
| Output port | CPU → world | `OUT` | 8 pins (`uo_out`) plus a strobe pin that toggles on every `OUT` | yes |
| Text output | CPU → PC | `CALL print` etc. (`lib/print.asm`) | `OUT` bytes, captured on the strobe and shown as characters | library on top of `OUT` |
| Input port | world → CPU | `IN` | 8 pins (`ui_in`), read at that moment | yes |
| Keyboard input | PC → CPU | `CALL getchar` / `getline` (`lib/input.asm`) | two RAM bytes that the RAM's owner fills in | no, a software convention |
| Halt | CPU → world | `HLT` | HALTED pin (`uio[4]`) goes high | yes |
| Memory access | PC ↔ RAM | (none) | the PC reads or writes the RAM directly | no, a loader feature |

Where each one is available:

| Channel | Simulator (`cpu8sim`) | Demo board (RP2040 + `cpu8load`) | Tang Nano 20K |
|---------|-----------------------|----------------------------------|---------------|
| Output port | printed as numbers | `-n` numbers, `-m` pin changes, 7-segment display | LEDs show bits 5..0 |
| Text output | `-a` | `-a` | UART to the PC, 115200 baud |
| Input port | `-i VALUE` | `-i VALUE`, or the DIP switches | button S2 is bit 1, the rest read 0 |
| Keyboard input | `-k`, or `-I 'text'` | `-k` | UART from the PC |
| Memory access | `-d ADDR:LEN` after the run | `-r ADDR:LEN` after the run; `W` / `R` commands at any time | no |

## Memory map

cpu8 sees 4 KB of RAM. Three areas have a fixed meaning:

| Address | Use |
|---------|-----|
| `0x000` | reset vector: execution starts here |
| `0x000`–`0xEFD` | program and data |
| `0xEFE` | mailbox flag: 0 = empty, not 0 = a byte is waiting |
| `0xEFF` | mailbox data: the input byte |
| `0xF00`–`0xFFF` | stack, grows down from `0xFFF` |

The mailbox addresses only matter to programs that use keyboard input. Other programs can use those two bytes freely.

## Output: `OUT`

`OUT` copies A to the 8 output pins (`uo_out`) and toggles the OUT_STROBE pin (`uio[5]`). The value stays on the pins until the next `OUT`.

The strobe exists so that a listener can count bytes. Without it, two equal values in a row (the two `l` in "hello") would look like one. A listener reads `uo_out` on every edge of the strobe, rising or falling.

An `OUT` takes at least 88 clocks, so at 10 MHz bytes are at least 8.8 µs apart.

### Text output

Text is just `OUT` bytes shown as characters. `programs/lib/print.asm` has the routines:

| Routine | Does | Arguments |
|---------|------|-----------|
| `print` | prints a NUL-terminated string of any length | A = high byte of the address, X = low byte |
| `print_dec` | prints a number in decimal, no leading zeros | A = value (0..255) |
| `print_hex` | prints a number as two hex digits | A = value |
| `newline` | prints `\n` | none |

All of them change A, X and the flags.

```
        LDI >msg        ; A = high byte of the address
        LDX <msg        ; X = low byte
        CALL print
        HLT
        .include "lib/print.asm"
msg:    .asciz "hello\n"
```

## Pin input: `IN`

`IN` copies the 8 input pins (`ui_in`) into A. It is a plain parallel port:

- It reads the level on the pins at that moment. There is no "new data" signal, so a program cannot tell a new byte from the same byte read twice.
- The pins are not synchronised to the clock. If they change at the instant `IN` samples them, A can hold a mix of old and new bits. For inputs that change while the program runs, read twice and use the value only when both reads agree.

It suits switches, buttons and a configuration value set before the run. `programs/bits.asm` uses it.

## Keyboard input: the mailbox

The chip has no instruction for text input. It does not need one: all of cpu8's memory is outside the chip, so whoever owns that memory can put bytes into it while the program runs. On the demo board that is the RP2040, on the Tang Nano the FPGA, and in the simulator the simulator itself. This document calls it the *host*.

Two RAM bytes form a mailbox:

| Address | Name | Written by the host | Written by the program |
|---------|------|---------------------|------------------------|
| `0xEFE` | flag | sets it to 1 after writing the data | writes 0 after reading the data |
| `0xEFF` | data | the next input byte | never |

The handshake for one byte:

```
host                                 program
----                                 -------
waits until flag == 0
writes the byte to data (0xEFF)
writes 1 to flag (0xEFE)
                                     waits until flag != 0
                                     reads data
                                     writes 0 to flag
waits until flag == 0
(next byte)
```

Each side writes one byte at a time, and the host always writes the data before the flag. So neither side can see half an update, and no locking is needed. A byte is never lost or read twice: the host does not send the next one until the program has cleared the flag.

`programs/lib/input.asm` has the routines:

| Routine | Does | Arguments | Returns |
|---------|------|-----------|---------|
| `getchar` | waits for one byte | none | A = the byte; X is kept |
| `getline` | reads a line and stores it as a NUL-terminated string, without the newline | A = high byte of the buffer address, X = low byte | A = length |

`getline` stores at most 31 characters, so the buffer needs 32 bytes. The rest of a longer line is dropped. Carriage returns are ignored, so both `\n` and `\r\n` line endings work.

Neither routine echoes what it reads. The terminal on the PC does that.

## Running a program with input and output

All examples use `programs/ask.asm`, which asks for a name and an age and prints both.

```sh
make -C tools
tools/cpu8asm -o ask.bin programs/ask.asm
```

### Simulator

```sh
tools/cpu8sim -a -k ask.bin                  # type the answers
tools/cpu8sim -a -I 'Ana\n42\n' ask.bin      # input given on the command line
```

- `-a` prints `OUT` bytes as text. Without it they are printed as numbers, one per line.
- `-k` takes keyboard input from standard input, read only when the program asks for it.
- `-I` takes the input from the argument. It understands `\n`, `\r`, `\t`, `\0`, `\\` and `\xHH`.
- `-i VALUE` sets the input port for `IN`.

### Demo board (RP2040)

Needs the firmware from `rp2040/` on the board's RP2040; see [rp2040/README.md](../rp2040/README.md).

```sh
tools/cpu8load -k ask.bin                    # type the answers
printf 'Ana\n42\n' | tools/cpu8load -k ask.bin
tools/cpu8load -a hello.bin                  # output only
```

- `-a` prints `OUT` bytes as text and waits for `HLT`. `-n` prints them as numbers.
- `-k` does what `-a` does and also sends what you type to the program. Type a line and press Enter. It waits for `HLT` with no time limit, unless you give `-t SECONDS`.
- `-i VALUE` sets the input port for `IN`.
- `-r ADDR:LEN` dumps RAM after the run.

### Tang Nano 20K

The program is built into the bitstream, and the PC talks to it with any serial terminal.

```sh
cd fpga && make PROG=../programs/ask.asm flash
picocom -b 115200 --echo --omap crlf /dev/ttyUSB1
```

Then press button S1. The program starts at power-on, so its first prompt is printed before the terminal is open; S1 restarts it.

- `--echo` shows what you type. `--omap crlf` sends Enter as a newline.
- The board shows up as two serial ports. Use the second one, usually `/dev/ttyUSB1`.
- LEDs show the last `OUT` value (bits 5..0). Button S2 is input port bit 1.

## Writing an interactive program

The whole of `ask.asm`, shortened to one question:

```
        LDI >q_name         ; print the question
        LDX <q_name
        CALL print

        LDI >name           ; read the answer into "name"
        LDX <name
        CALL getline

        LDI >m_hello        ; print "Hello, " + name + "!\n"
        LDX <m_hello
        CALL print
        LDI >name
        LDX <name
        CALL print
        LDI >m_end
        LDX <m_end
        CALL print
        HLT

        .include "lib/print.asm"
        .include "lib/input.asm"

q_name:  .asciz "What is your name? "
m_hello: .asciz "Hello, "
m_end:   .asciz "!\n"
name:    .space 32          ; getline needs 32 bytes
```

Rules to follow:

- Put the `.include` lines after the last instruction of your program. The libraries are code, and execution would run into them otherwise.
- Keep code and data below `0xEFE` when you use input, and always below `0xF00`.
- `print` and `getline` use self-modifying code, so they are not re-entrant. That only matters if you write interrupt-like tricks; cpu8 has no interrupts.
- For a non-blocking check ("is a key waiting?"), read the flag yourself: `LDA 0xEFE` then `JZ no_key`.

## How each platform implements it

### Simulator

`cpu8sim` keeps the 4 KB memory in an array. Before each instruction it checks the flag byte. With `-I` it fills the mailbox as soon as the flag is 0, like the firmware. With `-k` it reads standard input only when the next instruction reads an empty flag, so it never blocks before a prompt has been printed.

### Demo board

The RP2040 emulates the SPI RAM, so the RAM is an array in its memory.

- **Output:** a GPIO interrupt on both edges of OUT_STROBE reads `uo_out` into a 4 KB buffer. The firmware sends the bytes to the PC as `EV OUT <hex>` lines. If the PC reads too slowly and the buffer fills, bytes are dropped and counted (`lost=` in the `ST` reply).
- **Keyboard input:** `cpu8load -k` sends typed bytes with the `PUT <hex>` command into a 256-byte queue. The firmware's main loop moves one byte to the mailbox whenever the flag is 0. Reset empties the queue.
- **Input port:** the `UI <value>` command drives `ui_in`.
- **Memory access:** `W` and `R` write and read the RAM at any time, also while the CPU runs.

The full protocol is in [rp2040/README.md](../rp2040/README.md).

### Tang Nano 20K

The FPGA holds the CPU and the RAM (block RAM, preloaded with the program). The board's USB chip is a USB-to-serial bridge, so the same cable carries the text.

- **Output:** `fpga/uart_out.v` watches OUT_STROBE like the RP2040 does and sends each byte at 115200 baud (FPGA pin 69). A 1 KB FIFO absorbs bursts.
- **Keyboard input:** `fpga/uart_in.v` receives bytes (FPGA pin 70) into a 64-byte FIFO. In the RAM model, `0xEFE` and `0xEFF` are two registers beside the block RAM; the FPGA fills them from the FIFO with the same handshake. Button S1 (reset) also empties the FIFO and clears the flag.

None of this FPGA logic goes into the chip. `fpga/` only stands in for the demo board.

## Tests

A program's header comments tell the tests what to feed it and what to expect:

| Header | Meaning |
|--------|---------|
| `; expect: 1 2 3` | the `OUT` values, as numbers |
| `; expect-text: Hi\n` | the `OUT` values, as text |
| `; input: 0xB5` | value on the input port |
| `; stdin: Ana\n42\n` | keyboard input for the mailbox |
| `; limit: N` | endless program: run N instructions, compare the first outputs |

| Test | Command | Checks |
|------|---------|--------|
| Simulator | `programs/run_tests.sh` | output of every example against its header |
| Chip Verilog | `cd test && make` | every halting example: `OUT` values, full memory and clock count equal the simulator's. The testbench fills the mailbox like the firmware |
| Mock demo board | `cd rp2040/mock && make test` | `cpu8load` against the real firmware parser, including `-k` |
| FPGA top | `cd fpga && make sim PROG=../programs/ask.asm` | the UART output equals the simulator's; the `; stdin:` text is typed on the simulated UART |

## Limits

- **Not yet run on hardware.** Everything here is verified in simulation and on the mock board only.
- **Input that never comes.** `getchar` waits forever. `cpu8load -k -t SECONDS` and the simulator's instruction limit (`-n`) stop a stuck program.
- **Line length.** `getline` keeps 31 characters per line.
- **Typing ahead.** The demo board queues 256 bytes and the Tang Nano 64. `cpu8load` holds back what does not fit and sends it later; on the Tang Nano, bytes beyond the FIFO are dropped.
- **Output speed.** On the Tang Nano the UART sends about 11,500 bytes per second, and the CPU can produce them faster. Beyond the 1 KB FIFO, bytes are dropped.
- **No echo and no line editing in the program.** The terminal does both.
