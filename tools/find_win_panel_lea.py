"""Find rip-relative refs to cs_win_panel_match in client.dll."""
from __future__ import annotations

import struct
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_64

PE = Path(
    r"C:\Program Files (x86)\Steam\steamapps\common"
    r"\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"
)
NEEDLE = b"cs_win_panel_match\x00"


def main() -> None:
    data = PE.read_bytes()
    e = struct.unpack_from("<I", data, 0x3C)[0]
    num = struct.unpack_from("<H", data, e + 6)[0]
    size_opt = struct.unpack_from("<H", data, e + 20)[0]
    image_base = struct.unpack_from("<Q", data, e + 24 + 24)[0]
    sec = e + 24 + size_opt
    sections = []
    for i in range(num):
        o = sec + i * 40
        name = data[o : o + 8].split(b"\0", 1)[0].decode(errors="replace")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, o + 8)
        sections.append((name, vaddr, max(vsize, rsize), raddr, rsize))

    def off_to_rva(off: int) -> int | None:
        for _, vaddr, span, raddr, rsize in sections:
            if raddr <= off < raddr + rsize:
                return vaddr + (off - raddr)
        return None

    def rva_to_off(rva: int) -> int | None:
        for _, vaddr, span, raddr, rsize in sections:
            if vaddr <= rva < vaddr + span:
                return rva - vaddr + raddr
        return None

    soff = data.find(NEEDLE)
    srva = off_to_rva(soff)
    assert srva is not None
    sva = image_base + srva
    print(f"string VA={sva:#x} RVA={srva:#x}")

    text = next(s for s in sections if s[0].startswith(".text"))
    _, text_va, _, text_raw, text_rsize = text
    blob = data[text_raw : text_raw + text_rsize]
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True

    refs = []
    # Sliding window disasm is slow; instead scan for possible lea encodings.
    # Pattern: 48 8D 0D/15/05 xx xx xx xx  where target = ip+7+disp == sva
    for i in range(len(blob) - 7):
        b0, b1, b2 = blob[i], blob[i + 1], blob[i + 2]
        # REX.W lea r64, [rip+disp32]
        if b0 == 0x48 and b1 == 0x8D and (b2 & 0xC7) == 0x05:
            disp = struct.unpack_from("<i", blob, i + 3)[0]
            ip = image_base + text_va + i
            target = ip + 7 + disp
            if target == sva:
                refs.append(text_va + i)

    print(f"lea rip-rel refs: {len(refs)}")
    for rva in refs:
        off = rva_to_off(rva)
        assert off is not None
        print(f"\n=== ref @ RVA {rva:#x} ===")
        # show surrounding 0x80 bytes before for function context
        start = max(0, off - 0x40)
        for insn in md.disasm(data[start : off + 0x60], image_base + (rva - (off - start))):
            mark = " <<" if insn.address == image_base + rva else ""
            print(f"  {insn.address:#x}  {insn.mnemonic} {insn.op_str}{mark}")


if __name__ == "__main__":
    main()
