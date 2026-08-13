"""Locate cs_win_panel_match string and pointer xrefs in client.dll (evidence only)."""
from __future__ import annotations

import struct
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_64

PE = Path(
    r"C:\Program Files (x86)\Steam\steamapps\common"
    r"\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"
)
NEEDLE = b"cs_win_panel_match\x00"
IMAGE_BASE = 0x180000000  # may differ; we print both file offset and guessed RVA


def parse_sections(data: bytes):
    e = struct.unpack_from("<I", data, 0x3C)[0]
    num = struct.unpack_from("<H", data, e + 6)[0]
    size_opt = struct.unpack_from("<H", data, e + 20)[0]
    opt_magic = struct.unpack_from("<H", data, e + 24)[0]
    if opt_magic == 0x20B:
        image_base = struct.unpack_from("<Q", data, e + 24 + 24)[0]
    else:
        image_base = struct.unpack_from("<I", data, e + 24 + 28)[0]
    sec = e + 24 + size_opt
    sections = []
    for i in range(num):
        o = sec + i * 40
        name = data[o : o + 8].split(b"\0", 1)[0].decode(errors="replace")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, o + 8)
        sections.append((name, vaddr, max(vsize, rsize), raddr, rsize))
    return image_base, sections


def file_off_to_rva(sections, off: int) -> int | None:
    for name, vaddr, span, raddr, rsize in sections:
        if raddr <= off < raddr + rsize:
            return vaddr + (off - raddr)
    return None


def rva_to_off(sections, rva: int) -> int | None:
    for name, vaddr, span, raddr, rsize in sections:
        if vaddr <= rva < vaddr + span:
            return rva - vaddr + raddr
    return None


def main() -> None:
    data = PE.read_bytes()
    image_base, sections = parse_sections(data)
    print(f"image_base={image_base:#x}")
    for name, vaddr, span, raddr, rsize in sections:
        print(f"  {name:8} VA={vaddr:#x} size={span:#x} raw={raddr:#x}")

    hits = []
    start = 0
    while True:
        i = data.find(NEEDLE, start)
        if i < 0:
            break
        hits.append(i)
        start = i + 1
    print(f"string file offsets: {[hex(h) for h in hits]}")
    string_rvas = []
    for h in hits:
        rva = file_off_to_rva(sections, h)
        print(f"  file {h:#x} -> RVA {rva and hex(rva)} VA {(image_base + rva) if rva is not None else None}")
        if rva is not None:
            string_rvas.append(rva)

    # Find absolute VA pointers (8-byte LE) and RIP-relative lea targets in .text
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    text = next(s for s in sections if s[0].startswith(".text"))
    _, text_va, text_span, text_raw, text_rsize = text
    text_blob = data[text_raw : text_raw + text_rsize]

    for srva in string_rvas:
        sva = image_base + srva
        ptr = struct.pack("<Q", sva)
        ptr_hits = []
        pstart = 0
        while True:
            j = data.find(ptr, pstart)
            if j < 0:
                break
            prva = file_off_to_rva(sections, j)
            ptr_hits.append((j, prva))
            pstart = j + 1
        print(f"\nabsolute ptr to {sva:#x}: {len(ptr_hits)} hits")
        for j, prva in ptr_hits[:20]:
            print(f"  file {j:#x} rva {prva and hex(prva)}")

        # scan .text for lea/mov that resolve to sva via rip-relative
        print(f"scanning .text for rip-rel refs to string VA {sva:#x} ...")
        refs = []
        # Capstone full .text is heavy; search for disp32 candidates:
        # look for instructions containing the 4-byte pattern of (sva - (ip+7)) roughly
        # Better: walk with capstone in chunks
        offset = 0
        while offset < len(text_blob):
            chunk = text_blob[offset : offset + 0x10000]
            base = image_base + text_va + offset
            consumed = 0
            for insn in md.disasm(chunk, base):
                consumed = insn.address + insn.size - base
                if insn.op_str and hex(sva)[2:] in insn.op_str.replace("0x", ""):
                    # weak
                    pass
                # rip-relative: compute target
                if insn.disp != 0 and "rip" in insn.op_str:
                    target = insn.address + insn.size + insn.disp
                    if target == sva:
                        refs.append(insn.address - image_base)
            if consumed == 0:
                offset += 1
            else:
                offset += consumed
            if len(refs) >= 30:
                break
        print(f"  rip-rel refs found: {len(refs)}")
        for r in refs[:30]:
            print(f"    code RVA {r:#x}")
            off = rva_to_off(sections, r)
            assert off is not None
            for insn in md.disasm(data[off : off + 64], image_base + r):
                print(f"      {insn.address:#x}  {insn.mnemonic} {insn.op_str}")
                if insn.address > image_base + r + 48:
                    break


if __name__ == "__main__":
    main()
