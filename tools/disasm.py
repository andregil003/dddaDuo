#!/usr/bin/env python3
"""Disassemble around RVAs of DDDA.exe.

Usage:
    python tools/disasm.py 0x26A7C5
    python tools/disasm.py 0x26A7C5 --before 8 --after 24
    python tools/disasm.py --log path/to/ddpuck.log     # every "rva=0x..." in the log

Requires: capstone, pefile  (pip install capstone pefile)
"""

import argparse
import re
import sys

import capstone
import pefile

DEFAULT_EXE = r"C:\Program Files (x86)\Steam\steamapps\common\DDDA\DDDA.exe"


class Exe:
    def __init__(self, path):
        self.pe = pefile.PE(path, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.sections = []
        for s in self.pe.sections:
            self.sections.append((s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData, s.Name.rstrip(b"\0").decode("latin1")))
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = False

    def section_of(self, rva):
        for va, vsize, raw, rawsize, name in self.sections:
            if va <= rva < va + max(vsize, 1):
                return name
        return None

    def read(self, rva, size):
        for va, vsize, raw, rawsize, name in self.sections:
            if va <= rva < va + max(vsize, rawsize):
                off = raw + (rva - va)
                data = self.pe.__data__[off:off + size]
                if len(data) == size:
                    return data
        return None

    def disasm(self, rva, before=6, after=20):
        start = max(0, rva - before)
        data = self.read(start, (rva - start) + after)
        if data is None:
            print(f"  !! RVA 0x{rva:X} not mapped")
            return
        print(f"-- 0x{rva:08X} (VA 0x{self.base + rva:08X}, section {self.section_of(rva)}) --")
        for ins in self.md.disasm(data, start):
            mark = ">>" if ins.address == rva else "  "
            op = f"{ins.mnemonic} {ins.op_str}".strip()
            note = ""
            if ins.address == rva:
                if ins.mnemonic == "call":
                    m = re.match(r"0x([0-9a-f]+)", ins.op_str)
                    if m:
                        note = f"   ; -> RVA 0x{int(m.group(1), 16) - self.base:X}"
                elif ins.mnemonic.startswith("j"):
                    m = re.match(r"0x([0-9a-f]+)", ins.op_str)
                    if m:
                        tgt = int(m.group(1), 16) - self.base
                        note = f"   ; -> RVA 0x{tgt:X}"
            print(f"  {mark} 0x{ins.address:08X}  {ins.bytes.hex():<18} {op}{note}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rvas", nargs="*", help="RVA in hex, e.g. 0x26A7C5")
    ap.add_argument("--log", help="extract rva=0x... from a ddpuck.log")
    ap.add_argument("--exe", default=DEFAULT_EXE)
    ap.add_argument("--before", type=int, default=6)
    ap.add_argument("--after", type=int, default=20)
    args = ap.parse_args()

    targets = []
    for r in args.rvas:
        targets.append(int(r, 16))
    if args.log:
        text = open(args.log, "r", encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"rva=0x([0-9A-Fa-f]+)", text):
            v = int(m.group(1), 16)
            if v not in targets:
                targets.append(v)

    if not targets:
        print("nothing to disassemble")
        return 1

    exe = Exe(args.exe)
    print(f"image base 0x{exe.base:08X}  sections: " + ", ".join(s[4] for s in exe.sections))
    for rva in targets:
        print()
        exe.disasm(rva, args.before, args.after)
    return 0


if __name__ == "__main__":
    sys.exit(main())
