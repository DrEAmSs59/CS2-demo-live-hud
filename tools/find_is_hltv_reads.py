"""Find code sites that compare/read clientstate+0x2C3538 (is_hltv)."""
from __future__ import annotations

import struct
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_64

PE = Path(
    r"C:\Program Files (x86)\Steam\steamapps\common"
    r"\Counter-Strike Global Offensive\game\bin\win64\engine2.dll"
)
OFF = 0x2C3538
IMAGE_BASE = 0x180000000


def sections(data: bytes):
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    num = struct.unpack_from("<H", data, e_lfanew + 6)[0]
    size_opt = struct.unpack_from("<H", data, e_lfanew + 20)[0]
    sec = e_lfanew + 24 + size_opt
    out = []
    for i in range(num):
        o = sec + i * 40
        name = data[o : o + 8].split(b"\0", 1)[0].decode()
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, o + 8)
        out.append((name, vaddr, max(vsize, rsize), raddr, rsize))
    return out


def rva_to_off(secs, rva: int) -> int | None:
    for _, vaddr, span, raddr, _ in secs:
        if vaddr <= rva < vaddr + span:
            return rva - vaddr + raddr
    return None


def main() -> None:
    data = PE.read_bytes()
    secs = sections(data)
    # Pattern: 80 Bx ?? 38 35 2C 00   cmp byte ptr [r/m + 0x2C3538], imm
    # or      80 B8 38 35 2C 00
    needle = bytes([0x38, 0x35, 0x2C, 0x00])
    hits = []
    text = next(s for s in secs if s[0].startswith(".text"))
    _, vaddr, span, raddr, rsize = text
    blob = data[raddr : raddr + rsize]
    start = 0
    while True:
        i = blob.find(needle, start)
        if i < 0:
            break
        # look back a few bytes for cmp opcode
        for back in range(1, 8):
            j = i - back
            if j < 0:
                continue
            if blob[j] == 0x80:  # cmp r/m8, imm8
                rva = vaddr + j
                hits.append(rva)
                break
        start = i + 1

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    print(f"hits={len(hits)}")
    for rva in hits[:40]:
        off = rva_to_off(secs, rva)
        assert off is not None
        chunk = data[off : off + 24]
        ins = list(md.disasm(chunk, IMAGE_BASE + rva))
        line = " | ".join(f"{x.mnemonic} {x.op_str}" for x in ins[:3])
        print(f"  RVA {rva:#x}  {chunk[:12].hex()}  {line}")


if __name__ == "__main__":
    main()
