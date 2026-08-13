"""Find any insn using displacement 0x2C3538 in engine2 .text."""
from __future__ import annotations

import struct
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_64

PE = Path(
    r"C:\Program Files (x86)\Steam\steamapps\common"
    r"\Counter-Strike Global Offensive\game\bin\win64\engine2.dll"
)
NEEDLE = bytes([0x38, 0x35, 0x2C, 0x00])  # little-endian 0x2C3538
IMAGE_BASE = 0x180000000


def main() -> None:
    data = PE.read_bytes()
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    num = struct.unpack_from("<H", data, e_lfanew + 6)[0]
    size_opt = struct.unpack_from("<H", data, e_lfanew + 20)[0]
    sec = e_lfanew + 24 + size_opt
    text = None
    for i in range(num):
        o = sec + i * 40
        name = data[o : o + 8].split(b"\0", 1)[0].decode()
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, o + 8)
        if name.startswith(".text"):
            text = (vaddr, raddr, rsize)
            break
    assert text
    vaddr, raddr, rsize = text
    blob = data[raddr : raddr + rsize]
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    hits = []
    start = 0
    while True:
        i = blob.find(NEEDLE, start)
        if i < 0:
            break
        # decode from a window starting a bit earlier
        for back in range(0, 16):
            j = i - back
            if j < 0:
                continue
            insns = list(md.disasm(blob[j : j + 16], IMAGE_BASE + vaddr + j))
            if not insns:
                continue
            insn = insns[0]
            if NEEDLE.hex() in insn.bytes.hex() or (
                insn.op_str and "0x2c3538" in insn.op_str.lower()
            ):
                hits.append((vaddr + j, insn))
                break
        start = i + 1

    # unique by rva
    seen = set()
    print(f"unique sites ~{len(hits)}")
    for rva, insn in hits:
        if rva in seen:
            continue
        seen.add(rva)
        print(f"  {rva:#x}  {insn.bytes.hex():24}  {insn.mnemonic} {insn.op_str}")


if __name__ == "__main__":
    main()
