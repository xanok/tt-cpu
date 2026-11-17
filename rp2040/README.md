# cpu8 loader firmware for the Tiny Tapeout demo board

cpu8 has no memory on the chip. It runs from an SPI RAM, and on the TT demo board the board's microcontroller plays that RAM. The firmware builds for two boards:

- **RP2350B** (the default): the current "TT DB v3" / ETR demo board, which the GF180 shuttles ship with.
- **RP2040:** the older TT06+ demo board.

The folder is still called `rp2040/`, and the text below says "RP2040" where both chips behave the same. **The RP2350 build has not run anywhere yet:** not on a board, and not in an emulator (rp2040js only models the RP2040). The RP2040 build is checked in rp2040js (`emutest/`).

This firmware:

- **core1** runs [spi-ram-emu](https://github.com/MichaelBell/spi-ram-emu) by Michael Bell. It emulates a 64 KB 23LC512-style SPI RAM on the project's uio pins. This repo has a modified copy (see below).
- **core0** runs a USB serial (CDC) command handler. It can select the TT project, load a program into the emulated RAM, drive the project clock, ui_in and rst_n, report uo_out and HALTED, read RAM back, and stream uo_out changes. It also captures every byte the program writes with `OUT` (on the OUT_STROBE pin) and sends it to the host, which is how `cpu8load -a` prints a program's text output.

The PC side is [`../tools/cpu8load.c`](../tools/cpu8load.c).

```
cpu8load -p <project address> -w -m -r 0x800:32 prog.bin
```

## Files

| Path | What it is |
|------|------------|
| `src/pinmap.h` | Demo board GPIO map. All pin numbers live here. |
| `src/proto.c`, `proto.h` | Command parser in plain C. It is also compiled on the PC for the mock. |
| `src/hw.h` | Hardware interface the parser calls |
| `src/hw_pico.c` | RP2040 / RP2350 code for it: pins, PWM clock, project mux |
| `src/main.c` | Starts spi-ram-emu and USB, then loops over commands |
| `spi-ram-emu/` | Modified copy of `sram.c/.h/.pio` (BSD-3, see `LICENSE`), plus pico-sdk 2.x linker overrides in `ld/` |
| `boards/tt_demo_rp2350.h`, `boards/tt_demo_rp2040.h` | Board headers for the two demo boards |
| `mock/` | PC mock (the real parser plus fake hardware on a pty) and `test.sh` |

## Build

You need an ARM GCC and pico-sdk 2.x with the tinyusb submodule. On this machine both are installed without sudo:

```
export PATH=~/tools/xpack-arm-none-eabi-gcc-15.2.1-1.1/bin:$PATH
SDK="-DPICO_SDK_PATH=$HOME/tools/pico-sdk -DPICOTOOL_FETCH_FROM_GIT_PATH=$HOME/tools/picotool"

# current demo board (RP2350B), the default
cmake -B build_rp2350 $SDK
cmake --build build_rp2350 -j8 --target cpu8_loader    # -> build_rp2350/cpu8_loader.uf2

# older demo board (RP2040)
cmake -B build_rp2040 -DTT_BOARD=rp2040 $SDK
cmake --build build_rp2040 -j8 --target cpu8_loader    # -> build_rp2040/cpu8_loader.uf2
```

Toolchain setup used: xPack arm-none-eabi-gcc 15.2.1-1.1 (tarball unpacked into `~/tools`). pico-sdk 2.3.1 is a git clone in `~/tools/pico-sdk`, with `git submodule update --init lib/tinyusb`. On the first configure, the SDK fetches and builds picotool into `~/tools/picotool`.

**Flashing:** hold the demo board's BOOT button while plugging in USB, then copy `cpu8_loader.uf2` to the RPI-RP2 drive. Later you can send `BOOT` (or run `echo BOOT > /dev/ttyACM0`) to get back to the bootloader. This firmware replaces the TT MicroPython firmware. Flash the TT UF2 again to go back.

## Board and pin map

Both maps come from [tt-micropython-firmware](https://github.com/TinyTapeout/tt-micropython-firmware): `GPIOMapTTDBv3` in `src/ttboard/pins/gpio_map_dbv3.py` (commit `15ba5ce`) for the RP2350 board, and `GPIOMapTT06` in the last version before commit `37cf3e8` (2026-01-27, "Exclusive RP2350/ETR DB support: previous RP2040 support withdrawn") for the RP2040 board.

| Signal | RP2350B board (TT DB v3) | RP2040 board (TT06+) |
|--------|--------------------------|----------------------|
| project clk (PWM) | 16 | 0 |
| project rst_n | 14 | 1 |
| ctrl_ena / ctrl_sel_rst_n / ctrl_sel_inc | 0 / 1 / 2 | 4 / 2 / 3 |
| ui_in[0..7] | 17-24 | 9-12, 17-20 |
| uio[0..7] | 25-32 | 21-28 |
| uo_out[0..7] | 33-40 | 5-8, 13-16 |
| cpu8: CS_N, MOSI, MISO, SCK, HALTED, OUT_STROBE, MISO_RISE (uio0-6) | 25-31 | 21-27 |

- **RP2350 board:** uo_out is on GPIO 33-40, in the chip's high GPIO bank, so it is read through the 64-bit GPIO functions. The SPI pins are below 32, so the PIO programs need no GPIO base change. The board's other pins (manual clock button 15, analog current source 12, management pins 3-10) are left as inputs.
- **RP2350 clock:** the firmware runs the RP2350 at its rated 150 MHz, so the chip clock limit (sys/10) is 15 MHz and the 10 MHz default leaves 120 system clocks for the re-arm described below, against 160 on the RP2040 at 200 MHz. `SYSCLK 200000` (`cpu8load -s 200000`) raises it; that is overclocking, at 1.15 V.
- **TT04/TT05 boards are not supported.** They route rst_n and the mux control lines through an on-board analog mux that is shared with uo_out[0..3].
- **TT07 chips:** the TT07 carrier board swaps ctrl_sel_inc and ctrl_sel_rst_n. Set `TT07_CB_FIX 1` in `pinmap.h`.
- **Pico 2 on the bench:** `-DTT_BOARD=pico2` builds for a Raspberry Pi Pico 2 (RP2350A) wired to a Tang Nano 20K, with its own pin map and no project mux. See [docs/bench.md](../docs/bench.md).

**Project selection** copies `ProjectMux.reset_and_clock_mux()` from the TT firmware:
1. Set ctrl_ena=0 and ctrl_sel_rst_n=0, wait 10 ms.
2. Set ctrl_sel_rst_n=1, wait 10 ms.
3. Pulse ctrl_sel_inc `addr` times (1 ms high, 1 ms low).
4. Set ctrl_ena=1.

Selection takes about 20 ms + 2 ms × addr. Your project address is in the shuttle's project list (TT index / `info.yaml` of the shuttle repo).

**Reset:** rst_n is driven low to reset. To release it, the firmware drives it high briefly and then makes the pin an input, so the board's pull-up and reset button work as with the TT firmware. cpu8's reset starts without a clock, but it ends in step with the clock, so `GO` keeps reset low for at least 1 ms with the clock running.

**ui_in:** the RP2040 drives ui_in only after `SEL` or `ATTACH`. Set the demo board's ui_in DIP switches to OFF so they do not fight the RP2040. Please check this against your board's schematic.

## spi-ram-emu: changes and why

Upstream needs `SCK = MOSI + 1` and `CS = MOSI + 2`. cpu8's pins (CS=uio0, MOSI=uio1, MISO=uio2, SCK=uio3) give GPIO 21/22/23/24, so SCK = MOSI + 2 and CS = MOSI − 1, which **does not fit upstream**. MOSI must be the PIO IN base because of `in pins, 1`. So the copy here makes these changes, each marked `cpu8 change` in the source:

1. **Pin offsets.** The `wait pin` indexes in `sram.pio` come from `.define SRAM_SCK_OFS 2` and `SRAM_CS_OFS 31`. PIO input pin indexes wrap modulo 32, so index 31 from base 22 is GPIO 21. `sram.c` checks these against `pinmap.h` with `_Static_assert`. The init functions take each pin separately. CS, MOSI and SCK stay plain SIO inputs, because PIO can read any GPIO.
2. **MISO is not driven until the project is selected** (`sram_drive_miso()`). While another project is selected, its uio2 could be an output.
3. **MISO changes after the falling SCK edge, not the rising one.** Upstream shifts out right after the rising edge. That is valid for a master that samples on the rising edge. cpu8 samples MISO at the end of the SCK high phase (`src/cpu.v`, for pad-delay tolerance), so with upstream timing it would read the next bit. At 10 MHz the half period is 100 ns, while the emulator updates MISO roughly 30-60 ns after the edge. With the change the emulator behaves like a real 23LC512 and like `model/spi_ram_model.v`, which the cocotb tests use. As a side effect the first data bit gets half an SCK period more time. FAST READ keeps working.
4. **Linker.** Upstream's `sram_memmap.ld` targets pico-sdk 1.x. It is replaced by sdk 2.x overrides (`RAM_LENGTH=0x30000`, plus an `SPI_RAM` region at 0x20030000 and a `.spi_ram` section).

### MISO sample edge (`EDGE`, `cpu8load -e`)

The chip has a strap, MISO_RISE (uio6), which the firmware drives while the project is attached. `EDGE 0` (the default) makes cpu8 sample MISO at the end of the SCK high phase, which is what change 3 above is for. `EDGE 1` makes it sample at the SCK rising edge, like a standard mode-0 master. With `EDGE 1` the chip also works with the **stock** spi-ram-emu timing, so if the modified PIO program gives trouble on hardware, upstream's `sram.pio` output timing plus `EDGE 1` is the fallback. `EDGE 1` should work with the modified emulator too, as long as it answers within half an SCK period. `EDGE` holds the project in reset.

In simulation (`test/`, `make MISO_RISE=yes`) all tests pass with stock timing and the strap high, and the wrong pairings fail as expected.

## Timing limits (read this before choosing a clock)

- **Clock limit:** the firmware rejects a chip clock above sys/10, which is 20 MHz on the RP2040 board (200 MHz system clock) and 15 MHz on the RP2350 board (150 MHz). The limit comes from the CS re-arm time below, not from SCK: spi-ram-emu READ works up to SCK = sys/10 and cpu8 uses SCK = clk/2, which alone would allow sys/5. The RP2040 build runs at 200 MHz (`SYS_CLK_MHZ=200`, core at 1.15 V, flash at sys/4); the RP2350 build at its default 150 MHz.
- **CS re-arm time.** After CS goes high, core1 has to abort the DMA and restart both state machines before the next transaction's first SCK edge. The core1 re-arm path is about 40 instructions in the disassembly, roughly 50–65 cycles, plus input synchronisers and DMA abort latency. Michael Bell's README also estimates "around 50 SYS clocks". At 200 MHz that is about 300 ns.
  - cpu8 used to keep CS high for only 1–2 clocks, too short at 10 MHz. `src/cpu.v` now holds CS high for **at least 8 clocks** between transactions (`CS_GAP`): 800 ns at 10 MHz, or about 160 RP2040 clocks at 200 MHz. That leaves a good margin, so 10 MHz is the default.
  - In the rp2040js replay test (`emutest/`) the re-arm takes about 80 system clocks, and the emulator works at 10 system clocks per chip clock and fails at 8. So the 10 MHz default has a factor of two in hand. rp2040js counts CPU cycles only approximately, so this is indicative.
  - This is still the first thing to suspect if the CPU misbehaves on the board. Try a lower `-f` to check.
- uo_out streaming (`MON 1`) samples the pins in the core0 loop. Changes faster than the loop plus USB can carry are merged, and you see the latest value.

## Protocol

USB CDC, ASCII lines ending in `\n` (`\r` is also accepted). The firmware sends `\r\n`. Commands are case-insensitive. Numbers are decimal, or hex with `0x`. Data bytes are hex digit pairs, so binary payloads are safe.

Every non-empty command line gets **exactly one** final reply: `OK [result]` or `ERR <reason>`. Empty lines get no reply. A host can resynchronise by sending `\n`, discarding input and sending `PING`. Two other kinds of line can appear at any time, and a host must skip them while it waits for its reply:

- `# text`: help text (sent before `HELP`'s `OK`)
- `EV ...`: monitor events, only while `MON 1` is on and the project is out of reset:
  - `EV UO <hex> <us>`: uo_out changed; time in µs since `GO`
  - `EV HALT <us>`: HALTED went high
- `EV OUT <hexbytes>`: bytes the program wrote with `OUT`, in order. These lines are sent **always** while the project runs, not only with `MON 1`, at up to 64 bytes per line. All bytes captured before a command arrives are sent before that command's reply, so a host that reads `halted=1` in an `ST` reply already has the complete output.

### OUT capture

cpu8 toggles OUT_STROBE (uio5) on every `OUT`, so even repeated values are visible. A GPIO interrupt on both edges reads uo_out into a 4 KB ring buffer. It runs at the highest priority, so USB handling can't delay it, and it is a raw `IO_IRQ_BANK0` handler that runs entirely from RAM. An `OUT` takes at least 88 project clocks (8.8 µs at 10 MHz), and uo_out doesn't change until the next one, so the interrupt has time to spare. If the host doesn't read the buffer fast enough and it fills up, bytes are dropped and counted in `ST`'s `lost=` field. `GO` clears the buffer and the counter.

| Command | Reply | Notes |
|---------|-------|-------|
| `PING` | `OK cpu8-loader 1` | the last number is the protocol version |
| `ST` | `OK uo=XX halted=0/1 rst=0/1 clk=<Hz> ui=XX sel=<addr or -1> sys=<Hz> mon=0/1 t=<us> lost=<n> edge=0/1 inq=<n>` | t = µs since GO (0 in reset; wraps after 71 min); inq = input bytes still queued |
| `SEL addr` | `OK` | 0..1023. Holds reset, floats ui_in/MISO, runs the mux sequence, then drives ui_in and MISO |
| `ATTACH` / `DETACH` | `OK` | drive or float ui_in and MISO without selecting a project (for example when it is already selected) |
| `RST` | `OK` | hold rst_n low |
| `GO` | `OK` | rst_n low for ≥ max(1 ms, 10 clocks), then release. `ERR` if the clock is stopped |
| `CLK hz` | `OK <actual Hz>` | PWM on the clk pin: sys / (div × period), so for example 9.5 MHz gives 9523809. `0` stops it (pin low). `ERR` above sys/10 or below about 12 Hz |
| `UI val` | `OK` | 0..255 |
| `EDGE 0\|1` | `OK` | MISO_RISE strap; holds the project in reset |
| `W addr hex` | `OK <n>` | write up to 128 bytes |
| `R addr len` | `OK <hex>` | read 1..128 bytes |
| `FILL addr len val` | `OK` | for example `FILL 0 4096 0` |
| `PUT hex` | `OK <accepted> <free>` | queue input bytes for the program (see "Input mailbox"). Takes as many as fit in the 256-byte queue; send the rest again later. `ERR` in reset |
| `MON 0/1` | `OK` | uo_out/HALT event streaming |
| `SYSCLK khz` | `OK sys=<Hz> clk=<Hz>` | 50000..250000. Holds reset, then sets the clock up again if it still fits. Raises the core voltage for speeds above the chip's rating |
| `HELP` | `# ...` lines, then `OK` | |
| `BOOT` | `OK` | then reboots into the UF2 bootloader |

Addresses cover the whole 64 KB emulated RAM. cpu8 only sees 0x0000-0x0FFF and keeps its stack at 0xF00-0xFFF. RAM can be written and read while the CPU runs. That is useful for peeking at results, but it is not synchronised with the CPU.

### Input mailbox (`PUT`, `cpu8load -k`)

cpu8 has no text input, but the RP2040 is its RAM. Programs that use `programs/lib/input.asm` treat two RAM bytes as a mailbox: a flag at 0xEFE and a data byte at 0xEFF. In its main loop the firmware checks the flag; when it is 0 and the `PUT` queue is not empty, it writes the next byte to 0xEFF and then sets the flag to 1. The program waits for the flag, reads the byte and writes 0 to the flag. The data byte is always written before the flag, and each side only writes single bytes, so no locking is needed. Reset (`RST`, `GO`, `SEL`, `EDGE`, `SYSCLK`) empties the queue, so input is sent after `GO`.

`cpu8load -k` prints the program's output as text and sends everything that arrives on its standard input with `PUT`. On a terminal you type a line and press Enter; the terminal echoes it. Input can also be piped: `printf 'Ana\n42\n' | cpu8load -k ask.bin`. This is tested on the mock board, not yet on hardware.

The `cpu8load` sequence is: `PING`, [`SYSCLK`], `RST`, `SEL p` or `ATTACH`, `FILL 0 4096 0`, then `W` in 128-byte chunks, then `R` to read everything back and verify, `CLK f`, `UI v`, `EDGE e`, [`MON 1`], `GO`. Then it polls `ST` and/or follows `EV` lines until HALTED or the timeout, then `ST`, [`MON 0`], and `R` for each `-r` dump.

## Testing the emulator's PIO program without hardware

`emutest/` runs the real `sram.c` and `sram.pio` in [rp2040js](https://github.com/wokwi/rp2040js) and replays the SPI waveform recorded from the chip's Verilog into it. See [emutest/README.md](emutest/README.md).

## Testing without hardware

```
cd mock && make test
```

`cpu8mock` is the firmware's real `proto.c` linked with `hw_mock.c` on a pseudo-terminal. Its "chip" is the real cpu8 instruction model (`tools/cpu8core.h`, the same code as `cpu8sim`). It runs the loaded program at the project clock and captures `OUT` like the strobe interrupt does.

`test.sh` runs `cpu8load` against the mock:
- options, error messages, upload and verify;
- `-w`, `-m`, `-a`, `-r` and timeouts;
- clock rounding and `SYSCLK`;
- every halting example in `programs/`, checking that its `OUT` output through the board path equals `cpu8sim`'s;
- raw protocol checks for the error replies.

To try it by hand:

```
./cpu8mock -l /tmp/cpu8tty &  ./cpu8load -d /tmp/cpu8tty -p 5 -w -m -r 0xFF0:16 prog.bin
```

The firmware itself has only been built, never run on a board. The pin map, mux sequence, PIO changes and timing analysis above are untested on hardware.
