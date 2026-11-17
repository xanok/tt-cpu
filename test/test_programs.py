# SPDX-License-Identifier: Apache-2.0
"""Run every programs/*.asm on the RTL and compare it with the C simulator
(tools/cpu8sim): the sequence of OUT values, the whole 4 KB memory at HLT
and the exact number of clocks. OUT values are captured the way the RP2040
does it: on every edge of the OUT_STROBE pin (uio[5]), read uo_out.

Program header comments used here:
    ; input: N     value on ui_in (IN instruction)
    ; limit: N     endless program: skipped here (tested in the simulator only)
    ; stdin: TEXT  input for the mailbox (programs/lib/input.asm): fed into
                   the RAM model the way the RP2040 firmware does it
"""
import glob
import os
import re
import subprocess
import tempfile

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, Edge, FallingEdge, First, RisingEdge

from cpu8tools import MEM_SIZE, assemble_file, tool

CLK_NS = int(os.environ.get("CLK_NS", "100"))  # clock period, default 10 MHz

HERE = os.path.dirname(os.path.abspath(__file__))
PROGRAMS = sorted(glob.glob(os.path.join(HERE, "..", "programs", "*.asm")))
# PROGRAMS=hello,primes limits the run to those examples (handy on the
# gate-level netlist, where every program takes a while)
if os.environ.get("PROGRAMS"):
    _only = {n.strip() for n in os.environ["PROGRAMS"].split(",")}
    PROGRAMS = [p for p in PROGRAMS if os.path.basename(p)[:-4] in _only]


def header(path):
    meta = {}
    with open(path) as f:
        for line in f:
            m = re.match(r"\s*;\s*(input|limit)\s*:\s*(\S+)", line)
            if m:
                meta[m.group(1)] = int(m.group(2), 0)
            m = re.match(r"\s*;\s*stdin\s*:\s*(.*)", line)
            if m:
                meta["stdin"] = m.group(1).rstrip("\n")
    return meta


MBOX_FLAG, MBOX_DATA = 0xEFE, 0xEFF


async def feed_mailbox(dut, data):
    """Like the firmware: when the flag is 0, write the next byte, set the flag."""
    for byte in data:
        while True:
            await RisingEdge(dut.clk)
            if int(dut.ram.mem[MBOX_FLAG].value) == 0:
                break
        dut.ram.mem[MBOX_DATA].value = byte
        dut.ram.mem[MBOX_FLAG].value = 1
        await RisingEdge(dut.clk)


def reference(image, ui, stdin=""):
    """Run the C simulator: returns (outs, memory, clocks)."""
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "prog.bin")
        with open(path, "wb") as f:
            f.write(image)
        r = subprocess.run([tool("cpu8sim"), "-s", "-i", str(ui), "-I", stdin, "-d", f"0:{MEM_SIZE}", path],
                           capture_output=True, text=True, check=True)
    outs, memory = [], []
    for line in r.stdout.splitlines():
        if re.match(r"[0-9A-F]{3}:", line):
            memory += [int(b, 16) for b in line[4:].split()]
        else:
            outs.append(int(line))
    clocks = int(re.search(r"(\d+) clocks", r.stderr).group(1))
    return outs, memory, clocks


@cocotb.test()
@cocotb.parametrize(program=[os.path.basename(p) for p in PROGRAMS])
async def test_program(dut, program):
    path = os.path.join(HERE, "..", "programs", program)
    meta = header(path)
    if "limit" in meta:
        dut._log.info(f"{program}: endless program, simulator-only test")
        return
    ui = meta.get("input", 0)
    image = assemble_file(path)
    stdin = meta.get("stdin", "")
    ref_outs, ref_mem, ref_clocks = reference(image, ui, stdin)

    for i, byte in enumerate(image):
        dut.ram.mem[i].value = byte
    cocotb.start_soon(Clock(dut.clk, CLK_NS, unit="ns").start())
    dut.ena.value = 1
    dut.ui_in.value = ui
    dut.rst_n.value = 0
    await ClockCycles(dut.clk, 5)
    # Release reset in the middle of a clock period. Releasing it exactly on
    # the clock edge is a race on the gate-level netlist: the clock tree
    # delays the flip-flop's clock, so the chip would start one clock earlier
    # than the RTL and the clock count would differ by one.
    await FallingEdge(dut.clk)
    dut.rst_n.value = 1
    if stdin:
        cocotb.start_soon(feed_mailbox(dut, stdin.encode().decode("unicode_escape").encode("latin-1")))

    outs = []
    timeout = ref_clocks + 1000
    await ClockCycles(dut.clk, 4)  # the reset has reached every flip-flop: pins are defined
    while not int(dut.halted_pin.value):
        trig = await First(Edge(dut.strobe_pin), RisingEdge(dut.halted_pin),
                           ClockCycles(dut.clk, timeout))
        if isinstance(trig, Edge):
            # uo_out changes on the same clock edge as the strobe; on the
            # gate-level netlist the two arrive a few gate delays apart, so
            # read in the middle of the clock period, when both have settled
            await FallingEdge(dut.clk)
            outs.append(int(dut.uo_out.value))
            await RisingEdge(dut.clk)
        elif not int(dut.halted_pin.value):
            raise AssertionError(f"{program}: no HLT within {timeout} clocks")

    # HALTED and the testbench's clock counter change at the same clock
    # edge; read the counter half a clock later, when it has settled
    await FallingEdge(dut.clk)
    memory = [int(dut.ram.mem[i].value) for i in range(MEM_SIZE)]
    cycles = int(dut.cycles.value)
    dut._log.info(f"{program}: {len(outs)} outputs, {cycles} clocks")
    assert outs == ref_outs, f"OUT values differ: RTL {outs} vs sim {ref_outs}"
    diff = [hex(a) for a in range(MEM_SIZE) if memory[a] != ref_mem[a]]
    assert not diff, f"memory differs at {diff[:8]}"
    # +2: the reset synchroniser releases the CPU 2 clocks after rst_n
    assert cycles == ref_clocks + 2, f"clocks: RTL {cycles} vs sim {ref_clocks} + 2"
