#!/usr/bin/env python3
"""Two-pass assembler for cpu8.

Syntax (one statement per line, ';' starts a comment, case-insensitive):

    label:  LDI 0x10        ; immediates: 12, 0x0c, 0b1100, 'a', label, label+1
            STA count
            .org 0x100      ; set location counter
    count:  .byte 0, 1, 2   ; raw bytes

Usage:  asm.py prog.asm -o prog.hex     (hex = one byte per line, for $readmemh)
"""
import argparse
import re
import sys

ADDR_OPS = {"LDA": 0x1, "STA": 0x2, "ADD": 0x3, "SUB": 0x4, "AND": 0x5,
            "OR": 0x6, "XOR": 0x7, "JMP": 0x9, "JZ": 0xA, "JC": 0xB,
            "JNZ": 0xC, "CMP": 0xE}
IMM_OPS = {"LDI": 0x0, "ADI": 0x8}
MISC_OPS = {"OUT": 0xD0, "IN": 0xD1, "SHL": 0xD2, "SHR": 0xD3, "NOT": 0xD4,
            "NOP": 0xDF, "HLT": 0xF0}
MEM_SIZE = 4096


class AsmError(Exception):
    pass


def _value(expr, labels, lineno):
    expr = expr.strip()
    total = 0
    for sign, term in re.findall(r"([+-]?)\s*('.'|[^+\-\s]+)", expr):
        if term.startswith("'"):
            v = ord(term[1])
        elif re.fullmatch(r"[0-9][0-9a-fA-FxXbBoO_]*", term):
            v = int(term, 0)
        elif labels is None:
            v = 0  # pass 1: labels not known yet
        elif term.lower() in labels:
            v = labels[term.lower()]
        else:
            raise AsmError(f"line {lineno}: unknown label '{term}'")
        total += -v if sign == "-" else v
    return total


def assemble(src):
    """Return a bytearray of MEM_SIZE bytes."""
    lines = []
    for n, raw in enumerate(src.splitlines(), 1):
        line = raw.split(";", 1)[0].strip()
        while (m := re.match(r"([A-Za-z_]\w*):\s*", line)):
            lines.append((n, "label", m.group(1).lower(), None))
            line = line[m.end():]
        if line:
            parts = line.split(None, 1)
            lines.append((n, "stmt", parts[0].upper(), parts[1] if len(parts) > 1 else ""))

    labels = {}
    for pass_no in (1, 2):
        mem = bytearray(MEM_SIZE)
        pc = 0
        known = None if pass_no == 1 else labels
        for n, kind, name, args in lines:
            if kind == "label":
                if pass_no == 1:
                    if name in labels:
                        raise AsmError(f"line {n}: duplicate label '{name}'")
                    labels[name] = pc
                continue
            if name == ".ORG":
                pc = _value(args, known, n)
                continue
            if name == ".BYTE":
                out = [_value(a, known, n) & 0xFF for a in args.split(",")]
            elif name in ADDR_OPS:
                ea = _value(args, known, n)
                if pass_no == 2 and not 0 <= ea < MEM_SIZE:
                    raise AsmError(f"line {n}: address {ea:#x} out of range")
                out = [(ADDR_OPS[name] << 4) | ((ea >> 8) & 0xF), ea & 0xFF]
            elif name in IMM_OPS:
                imm = _value(args, known, n)
                if pass_no == 2 and not -128 <= imm <= 255:
                    raise AsmError(f"line {n}: immediate {imm} out of range")
                out = [IMM_OPS[name] << 4, imm & 0xFF]
            elif name in MISC_OPS:
                if args:
                    raise AsmError(f"line {n}: {name} takes no operand")
                out = [MISC_OPS[name], 0]
            else:
                raise AsmError(f"line {n}: unknown mnemonic '{name}'")
            if pc + len(out) > MEM_SIZE:
                raise AsmError(f"line {n}: program exceeds {MEM_SIZE} bytes")
            mem[pc:pc + len(out)] = bytes(out)
            pc += len(out)
    return mem


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("src")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()
    try:
        with open(args.src) as f:
            mem = assemble(f.read())
    except AsmError as e:
        sys.exit(f"{args.src}: {e}")
    with open(args.output, "w") as f:
        f.write("".join(f"{b:02x}\n" for b in mem))


if __name__ == "__main__":
    main()
