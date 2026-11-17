# SPDX-License-Identifier: Apache-2.0
"""Helpers that call the C tools in ../tools (built by the test Makefile)."""
import os
import subprocess
import tempfile

TOOLS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools")
MEM_SIZE = 4096


def tool(name):
    return os.path.join(TOOLS, name)


def assemble_file(path):
    """Assemble a .asm file, return the 4 KB memory image (bytes)."""
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "prog.bin")
        subprocess.run([tool("cpu8asm"), "-o", out, path], check=True)
        with open(out, "rb") as f:
            image = f.read()
    return image + bytes(MEM_SIZE - len(image))


def assemble(src):
    """Assemble source text, return the 4 KB memory image (bytes)."""
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "prog.asm")
        with open(path, "w") as f:
            f.write(src)
        return assemble_file(path)
