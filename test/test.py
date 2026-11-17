# SPDX-License-Identifier: Apache-2.0
"""cocotb tests for cpu8. Each test assembles a small program, loads it into
the SPI RAM model, runs until HLT and checks outputs / memory."""

import os

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, RisingEdge, Timer

from cpu8tools import assemble

HALTED = 4  # uio_out bit
GATES = os.environ.get("GATES") == "yes"  # running on the gate-level netlist
CLK_NS = int(os.environ.get("CLK_NS", "100"))  # clock period, default 10 MHz


def u(sig):
    return sig.value.to_unsigned()


async def boot(dut, src, ui=0):
    """Load program, reset the CPU and start the clock."""
    image = assemble(src)
    for i, byte in enumerate(image):  # whole 4 KB: clears leftovers from earlier tests
        dut.ram.mem[i].value = byte
    clock = cocotb.start_soon(Clock(dut.clk, CLK_NS, unit="ns").start())  # 10 MHz
    dut.ena.value = 1
    dut.ui_in.value = ui
    dut.rst_n.value = 0
    await ClockCycles(dut.clk, 5)
    dut.rst_n.value = 1
    return clock


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
        ANI 0x64
        STA r_and
        LDA x
        ORI 0x64
        STA r_or
        LDA x
        XRI 0x64
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
async def test_input_port_changes(dut):
    """IN reads the pins through the synchroniser: a new value is seen by the
    next IN, also when the pins change away from a clock edge."""
    await boot(dut, """
loop:   IN
        OUT
        CPI 0x99
        JNZ loop
        HLT
""", ui=0x11)
    seen = []
    for value in (0x22, 0x7E, 0x99):
        await ClockCycles(dut.clk, 700)
        seen.append(u(dut.uo_out))
        await Timer(37, unit="ns")      # not aligned with the clock
        dut.ui_in.value = value
    await run_to_halt(dut)
    assert seen == [0x11, 0x22, 0x7E]
    assert u(dut.uo_out) == 0x99


@cocotb.test(skip=GATES)
async def test_unused_state_recovers(dut):
    """Unused state codes 6 and 7 go back to fetching (source only: the
    netlist has no signal called state)."""
    for code in (6, 7):
        await boot(dut, """
        LDI 0
loop:   ADI 1
        OUT
        CPI 50
        JNZ loop
        HLT
""")
        state = dut.user_project.core.state
        await ClockCycles(dut.clk, 3000)
        while state.value.to_unsigned() != 1:   # S_FWAIT: nothing executed yet
            await RisingEdge(dut.clk)
        state.value = code
        await ClockCycles(dut.clk, 1)
        assert state.value.to_unsigned() == code
        await run_to_halt(dut)
        assert u(dut.uo_out) == 50


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


@cocotb.test()
async def test_immediates(dut):
    """SBI / CPI / CPX flags and results."""
    await boot(dut, """
        LDI 10
        SBI 3           ; 7, C=0
        JC  fail
        STA r0
        SBI 8           ; 7-8 = 0xFF, borrow
        JC  b1
        JMP fail
b1:     STA r1
        CPI 0xFF        ; equal: Z=1, C=0, A unchanged
        JNZ fail
        JC  fail
        LDX 3
        CPX 4           ; 3 < 4: C=1
        JC  b2
        JMP fail
b2:     CPX 3
        JNZ fail
        STA r2          ; still 0xFF
        LDI 0x5A
        OUT
        HLT
fail:   LDI 0xEE
        OUT
        HLT
        .org 0x200
r0:     .byte 0
r1:     .byte 0
r2:     .byte 0
""")
    await run_to_halt(dut)
    assert u(dut.uo_out) == 0x5A
    base = 0x200
    assert [mem(dut, base + i) for i in range(3)] == [7, 0xFF, 0xFF]


@cocotb.test()
async def test_adc_sbc(dut):
    """ADC / SBC: carry in, carry out, borrow in, borrow out, Z."""
    await boot(dut, """
        LDI 0xFF
        ADI 1           ; A=0, C=1
        ADC 5           ; 0+5+1 = 6, C=0
        STA r0
        ADC 5           ; 6+5+0 = 11
        STA r1
        LDI 0xFF
        ADI 1           ; C=1
        LDI 0xFF        ; LDI keeps C
        ADC 0           ; 0xFF+0+1 = 0, C=1, Z=1
        JNZ fail
        JC  c1
        JMP fail
c1:     STA r2
        ADC 0xFF        ; 0+0xFF+1 = 0x100: A=0, C=1
        JNZ fail
        JC  c2
        JMP fail
c2:     LDI 10
        SBC 3           ; 10-3-1 = 6, C=0
        JC  fail
        STA r3
        SBC 3           ; 6-3-0 = 3
        STA r4
        SBI 4           ; 3-4 = 0xFF, C=1
        LDI 0
        SBC 0           ; 0-0-1 = 0xFF, C=1
        JC  c3
        JMP fail
c3:     STA r5
        LDI 1
        SBC 0           ; 1-0-1 = 0, C=0, Z=1
        JNZ fail
        JC  fail
        SBI 1           ; C=1
        LDI 0
        SBC 0xFF        ; 0-0xFF-1 = -0x100: A=0, C=1
        JNZ fail
        JC  c4
        JMP fail
c4:     LDI 0x5A
        OUT
        HLT
fail:   LDI 0xEE
        OUT
        HLT
        .org 0x200
r0:     .byte 0
r1:     .byte 0
r2:     .byte 0xAA
r3:     .byte 0
r4:     .byte 0
r5:     .byte 0
""")
    await run_to_halt(dut)
    assert u(dut.uo_out) == 0x5A
    base = 0x200
    assert [mem(dut, base + i) for i in range(6)] == [6, 11, 0, 6, 3, 0xFF]


@cocotb.test()
async def test_index_register(dut):
    """Copy a table with LDA ea,X / STA ea,X, sum it with ADD ea,X."""
    await boot(dut, """
        LDX 4
copy:   DEX
        LDA src,X
        STA dst,X
        TXA
        JNZ copy
        LDI 0
        LDX 0
sum:    ADD dst,X
        INX
        CPX 4
        JNZ sum
        OUT             ; 1+2+3+250 = 256 -> 0, carry from last add
        TAX
        TXA
        HLT
        .org 0x300
src:    .byte 1, 2, 3, 250
        .org 0x8FE      ; dst crosses a page boundary: ea + X carries into hi
dst:    .byte 0, 0, 0, 0
""")
    await run_to_halt(dut)
    assert [mem(dut, 0x8FE + i) for i in range(4)] == [1, 2, 3, 250]
    assert u(dut.uo_out) == 0


@cocotb.test()
async def test_x_12bit(dut):
    """X is 12 bits: LDXH / TAXH / TXHA, INX / DEX across 0xFF, CPX on all bits."""
    await boot(dut, """
        LDX 0xFF
        INX             ; 0x100: not zero
        JZ  fail
        TXA             ; low byte 0: Z
        JNZ fail
        TXHA            ; 1
        STA r0
        LDA 0x400,X     ; M[0x500]
        STA r1
        DEX             ; 0x0FF
        TXHA            ; 0: Z
        JNZ fail
        LDX 0
        DEX             ; 0xFFF
        JZ  fail
        TXHA
        STA r2          ; 0x0F
        LDA 0x501,X     ; wraps at 4 KB: M[0x500]
        STA r3
        INX             ; 0: Z
        JNZ fail
        LDX 0x34
        LDXH 0xA5       ; only 4 bits: X = 0x534
        LDI 0x77
        STA 0,X         ; M[0x534]
        CPX 0x34        ; X is 0x534, above any immediate: no Z, no C
        JZ  fail
        JC  fail
        LDXH 0
        CPX 0x34        ; equal
        JNZ fail
        CPX 0x35        ; below: C
        JC  c1
        JMP fail
c1:     LDI 0xF6        ; pointer from two bytes, A = high
        LDX 0x02
        TAXH            ; X = 0x602, flags kept
        JC  c2
        JMP fail
c2:     LDA 0,X         ; M[0x602]
        STA r4
        LDI 9
        TAX             ; TAX clears the high bits
        TXHA
        JNZ fail
        LDI 0x5A
        OUT
        HLT
fail:   LDI 0xEE
        OUT
        HLT
        .org 0x200
r0:     .byte 0
r1:     .byte 0
r2:     .byte 0
r3:     .byte 0
r4:     .byte 0
        .org 0x500
        .byte 0xC3
        .org 0x602
        .byte 0x3C
""")
    await run_to_halt(dut)
    assert u(dut.uo_out) == 0x5A
    assert [mem(dut, 0x200 + i) for i in range(5)] == [1, 0xC3, 0x0F, 0xC3, 0x3C]
    assert mem(dut, 0x534) == 0x77


@cocotb.test()
async def test_call_ret_stack(dut):
    """Nested CALLs, PUSH/POP and the return addresses in the stack page."""
    await boot(dut, """
        LDI 1
        CALL f          ; f: A*2 via g, pushes/pops around
        OUT             ; 1 -> f -> 2+1 = 3 ... see below
        HLT
f:      PUSH            ; save 1
        CALL g          ; A = 2
        STA t
        POP             ; A = 1
        ADD t           ; 1 + 2 = 3
        RET
g:      SHL
        RET
t:      .byte 0
""")
    await run_to_halt(dut)
    assert u(dut.uo_out) == 3
    # outer CALL at 0x002 pushed return 0x004 at 0xFFE/0xFFF (high byte first)
    assert mem(dut, 0xFFE) == 0x00 and mem(dut, 0xFFF) == 0x04
    # inner CALL at 0x00A pushed 0x00C below the saved A (0xFFD = 1)
    assert mem(dut, 0xFFD) == 1
    assert mem(dut, 0xFFB) == 0x00 and mem(dut, 0xFFC) == 0x0C


@cocotb.test()
async def test_recursion_deep(dut):
    """Recursive countdown 100 levels deep (200 stack bytes)."""
    await boot(dut, """
        LDI 100
        CALL down
        LDA depth
        OUT
        HLT
down:   JZ  done
        PUSH
        LDA depth
        ADI 1
        STA depth
        POP
        ADI -1
        CALL down
done:   RET
depth:  .byte 0
""")
    await run_to_halt(dut, max_cycles=2_000_000)
    assert u(dut.uo_out) == 100


@cocotb.test()
async def test_out_strobe(dut):
    """OUT_STROBE (uio[5]) toggles on every OUT, also when the value repeats,
    and is an output; HALTED (uio[4]) too."""
    await boot(dut, "LDI 'a'\nOUT\nOUT\nOUT\nLDI 'b'\nOUT\nHLT")
    assert (u(dut.uio_oe) >> 4) & 0b11 == 0b11
    toggles, last = 0, (u(dut.uio_out) >> 5) & 1
    for _ in range(5000):
        await RisingEdge(dut.clk)
        s = (u(dut.uio_out) >> 5) & 1
        if s != last:
            toggles += 1
            last = s
        if (u(dut.uio_out) >> HALTED) & 1:
            break
    assert toggles == 4
    assert u(dut.uo_out) == ord("b")


@cocotb.test()
async def test_reset_mid_run(dut):
    """Reset pulses at many points of a running program (also in the middle of
    SPI transactions, and not aligned to the clock): the program must restart
    from address 0 every time and give the right result."""
    from cocotb.triggers import Timer
    await boot(dut, """
        LDI 0x11
        STA v
        CALL f
        OUT
        HLT
f:      LDA v
        ADI 1
        RET
v:      .byte 0
""")
    for wait in (1, 2, 3, 7, 40, 83, 95, 170, 251, 333, 420, 505):
        await ClockCycles(dut.clk, wait)
        await Timer(37, unit="ns")       # release/assert away from the clock edge
        dut.rst_n.value = 0
        await Timer(350, unit="ns")      # 3.5 clocks
        dut.rst_n.value = 1
    await run_to_halt(dut)
    assert u(dut.uo_out) == 0x12
    assert mem(dut, 0xFFE) == 0x00 and mem(dut, 0xFFF) == 0x06  # one CALL, SP reset


@cocotb.test()
async def test_reset_without_clock(dut):
    """Reset needs no clock: with the clock stopped in the middle of an SPI
    transaction, rst_n low alone puts every output in its reset state."""
    clock = await boot(dut, """
        LDI 0x5A
        OUT
        LDA v
        OUT
        HLT
v:      .byte 0x33
""")
    for _ in range(2000):  # until the first OUT is done and a transaction is under way
        await RisingEdge(dut.clk)
        uio = u(dut.uio_out)
        if u(dut.uo_out) == 0x5A and (uio & 0b0001) == 0:
            break
    else:
        raise AssertionError("no SPI transaction after the first OUT")
    await ClockCycles(dut.clk, 20)  # well into the transaction
    clock.cancel()  # the clock stops here, with CS low
    await Timer(30, unit="ns")
    assert u(dut.uo_out) == 0x5A and (u(dut.uio_out) & 0b0001) == 0

    dut.rst_n.value = 0
    await Timer(100, unit="ns")  # no clock edge in here
    uio = u(dut.uio_out)
    assert (uio & 0b0001) == 0b0001, "CS must be high"
    assert (uio & 0b1000) == 0, "SCK must be low"
    assert (uio & 0b110000) == 0, "HALTED and OUT_STROBE must be low"
    assert u(dut.uo_out) == 0

    # Clock back on, reset released: the program runs from the start
    cocotb.start_soon(Clock(dut.clk, CLK_NS, unit="ns").start())
    await ClockCycles(dut.clk, 3)
    dut.rst_n.value = 1
    await run_to_halt(dut)
    assert u(dut.uo_out) == 0x33
