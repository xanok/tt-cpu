# SPDX-License-Identifier: Apache-2.0
"""Run every programs/*.asm on the RTL and compare it with the C simulator
(tools/cpu8sim): the sequence of OUT values, the whole 4 KB memory at HLT
and the exact number of clocks.

Program header comments used here:
    ; input: N     value on ui_in (IN instruction)
    ; limit: N     endless program: skipped here (tested in the simulator only)
"""
import glob
import os
import re
import subprocess
import tempfile

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, First, RisingEdge

from cpu8tools import MEM_SIZE, assemble_file, tool

HERE = os.path.dirname(os.path.abspath(__file__))
PROGRAMS = sorted(glob.glob(os.path.join(HERE, "..", "programs", "*.asm")))


def header(path):
    meta = {}
    with open(path) as f:
        for line in f:
            m = re.match(r"\s*;\s*(input|limit)\s*:\s*(\S+)", line)
            if m:
                meta[m.group(1)] = int(m.group(2), 0)
    return meta


def reference(image, ui):
    """Run the C simulator: returns (outs, memory, clocks)."""
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "prog.bin")
        with open(path, "wb") as f:
            f.write(image)
        r = subprocess.run([tool("cpu8sim"), "-s", "-i", str(ui), "-d", f"0:{MEM_SIZE}", path],
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
    ref_outs, ref_mem, ref_clocks = reference(image, ui)

    for i, byte in enumerate(image):
        dut.ram.mem[i].value = byte
    cocotb.start_soon(Clock(dut.clk, 100, unit="ns").start())
    dut.ena.value = 1
    dut.ui_in.value = ui
    dut.rst_n.value = 0
    await ClockCycles(dut.clk, 5)
    dut.rst_n.value = 1

    outs = []
    timeout = ref_clocks + 1000
    while not int(dut.halted_pin.value):
        await First(RisingEdge(dut.out_strobe), RisingEdge(dut.halted_pin),
                    ClockCycles(dut.clk, timeout))
        if int(dut.out_strobe.value):
            outs.append(int(dut.out_value.value))
            await RisingEdge(dut.clk)  # leave the strobe cycle
        elif not int(dut.halted_pin.value):
            raise AssertionError(f"{program}: no HLT within {timeout} clocks")

    memory = [int(dut.ram.mem[i].value) for i in range(MEM_SIZE)]
    cycles = int(dut.cycles.value)
    dut._log.info(f"{program}: {len(outs)} outputs, {cycles} clocks")
    assert outs == ref_outs, f"OUT values differ: RTL {outs} vs sim {ref_outs}"
    diff = [hex(a) for a in range(MEM_SIZE) if memory[a] != ref_mem[a]]
    assert not diff, f"memory differs at {diff[:8]}"
    assert cycles == ref_clocks, f"clocks: RTL {cycles} vs sim {ref_clocks}"
