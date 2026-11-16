# SPDX-License-Identifier: Apache-2.0
"""cocotb tests for cpu8. Each test assembles a small program, loads it into
the SPI RAM model, runs until HLT and checks outputs / memory."""

import os
import sys

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, RisingEdge

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from asm import assemble  # noqa: E402

HALTED = 4  # uio_out bit


def u(sig):
    return sig.value.to_unsigned()


async def boot(dut, src, ui=0):
    """Load program, reset the CPU and start the clock."""
    image = assemble(src)
    for i, byte in enumerate(image):  # whole 4 KB: clears leftovers from earlier tests
        dut.ram.mem[i].value = byte
    cocotb.start_soon(Clock(dut.clk, 100, unit="ns").start())  # 10 MHz
    dut.ena.value = 1
    dut.ui_in.value = ui
    dut.rst_n.value = 0
    await ClockCycles(dut.clk, 5)
    dut.rst_n.value = 1


async def run_to_halt(dut, max_cycles=200_000):
    for _ in range(max_cycles):
        await RisingEdge(dut.clk)
        if (u(dut.uio_out) >> HALTED) & 1:
            return
    raise AssertionError("CPU did not halt")


def mem(dut, addr):
    return u(dut.ram.mem[addr])


@cocotb.test()
async def test_ldi_out(dut):
    await boot(dut, "LDI 0x5A\nOUT\nHLT")
    await run_to_halt(dut)
    assert u(dut.uo_out) == 0x5A


@cocotb.test()
async def test_alu_memory(dut):
    await boot(dut, """
        LDA x
        ADD y      ; 0xC8 + 0x64 = 0x12C -> 0x2C, C=1
        STA r_add
        LDA x
        SUB y      ; 0x64
        STA r_sub
        LDA x
        AND y
        STA r_and
        LDA x
        OR y
        STA r_or
        LDA x
        XOR y
        STA r_xor
        LDI 0x0F
        NOT
        STA r_not
        HLT
        .org 0x200
x:      .byte 0xC8
y:      .byte 0x64
r_add:  .byte 0
r_sub:  .byte 0
r_and:  .byte 0
r_or:   .byte 0
r_xor:  .byte 0
r_not:  .byte 0
""")
    await run_to_halt(dut)
    x, y = 0xC8, 0x64
    assert mem(dut, 0x202) == (x + y) & 0xFF
    assert mem(dut, 0x203) == x - y
    assert mem(dut, 0x204) == x & y
    assert mem(dut, 0x205) == x | y
    assert mem(dut, 0x206) == x ^ y
    assert mem(dut, 0x207) == 0xF0


@cocotb.test()
async def test_loop_sum(dut):
    """Sum 1..10 with a countdown loop -> 55."""
    await boot(dut, """
        LDI 10
        STA n
loop:   LDA sum
        ADD n
        STA sum
        LDA n
        ADI -1
        STA n
        JNZ loop
        LDA sum
        OUT
        HLT
n:      .byte 0
sum:    .byte 0
""")
    await run_to_halt(dut)
    assert u(dut.uo_out) == 55


@cocotb.test()
async def test_flags_and_branches(dut):
    """Each check ORs a bit into 'ok' only if the branch behaved correctly."""
    await boot(dut, """
        LDI 0xFF
        ADI 1          ; -> 0, Z=1 C=1
        JC  c1
        JMP fail
c1:     JZ  z1
        JMP fail
z1:     LDI 5
        CMP five       ; equal: Z=1 C=0, A unchanged
        JC  fail
        JNZ fail
        CMP six        ; 5-6 borrows: C=1
        JC  b1
        JMP fail
b1:     SHL            ; A was 5 -> 10, C=0
        JC  fail
        LDI 0x81
        SHR            ; -> 0x40, C=1
        JC  s1
        JMP fail
s1:     OUT            ; 0x40
        HLT
fail:   LDI 0xEE
        OUT
        HLT
five:   .byte 5
six:    .byte 6
""")
    await run_to_halt(dut)
    assert u(dut.uo_out) == 0x40


@cocotb.test()
async def test_input_port(dut):
    await boot(dut, "IN\nADI 1\nOUT\nHLT", ui=0x41)
    await run_to_halt(dut)
    assert u(dut.uo_out) == 0x42


@cocotb.test()
async def test_counter_outputs(dut):
    """The output port should step 0,1,2,3... like the FPGA blink demo."""
    await boot(dut, """
loop:   OUT
        ADI 1
        JMP loop
""")
    seen = []
    for _ in range(20_000):
        await RisingEdge(dut.clk)
        v = u(dut.uo_out)
        if not seen or seen[-1] != v:
            seen.append(v)
        if len(seen) == 6:
            break
    assert seen == [0, 1, 2, 3, 4, 5]


async def cycles_to_halt(dut, max_cycles=200_000):
    for n in range(max_cycles):
        await RisingEdge(dut.clk)
        if (u(dut.uio_out) >> HALTED) & 1:
            return n
    raise AssertionError("CPU did not halt")


@cocotb.test()
async def test_strlen(dut):
    """STRLEN on several strings, results stored to memory; the code after
    each STRLEN must keep running normally (CS released correctly)."""
    await boot(dut, """
        STRLEN hello
        STA r0
        STRLEN empty
        JNZ fail        ; empty string sets Z
        STA r1
        STRLEN odd      ; string at an odd address in another page
        STA r2
        JC fail         ; NUL found -> C clear
        LDI 0x77
        OUT
        HLT
fail:   LDI 0xEE
        OUT
        HLT
r0:     .byte 0xAA
r1:     .byte 0xAA
r2:     .byte 0xAA
hello:  .asciz "hello"
empty:  .asciz ""
        .org 0x3A1
odd:    .asciz "a;b tiny tapeout!"
""")
    await run_to_halt(dut)
    assert u(dut.uo_out) == 0x77
    base = 0x1C  # r0 address (14 two-byte instructions)
    assert [mem(dut, base + i) for i in range(3)] == [5, 0, 17]


@cocotb.test()
async def test_strlen_saturates(dut):
    """No NUL within 255 bytes: A = 255 and C = 1."""
    src = "STRLEN s\nJC ok\nLDI 0xEE\nOUT\nHLT\nok: OUT\nHLT\n.org 0x100\ns: .byte " + \
          ", ".join(["0x41"] * 300) + ", 0"
    await boot(dut, src)
    await run_to_halt(dut)
    assert u(dut.uo_out) == 255


@cocotb.test()
async def test_strlen_speed(dut):
    """Hardware STRLEN vs the equivalent software loop on a 64-char string."""
    string = ".org 0x200\ns: .asciz \"" + "x" * 64 + "\"\n"
    await boot(dut, "STRLEN s\nOUT\nHLT\n" + string)
    hw = await cycles_to_halt(dut)
    assert u(dut.uo_out) == 64

    # software: n = 0; while (*p) { n++; p++ } using self-modifying LDA
    await boot(dut, """
loop:   LDA s           ; operand byte patched below
        JZ done
        LDA n
        ADI 1
        STA n
        LDA loop+1
        ADI 1
        STA loop+1
        JMP loop        ; (fits: 64-char string stays within one page)
done:   LDA n
        OUT
        HLT
n:      .byte 0
""" + string)
    sw = await cycles_to_halt(dut)
    assert u(dut.uo_out) == 64
    dut._log.info(f"strlen(64 chars): hardware {hw} cycles, software {sw} cycles "
                  f"({sw / hw:.1f}x faster)")
