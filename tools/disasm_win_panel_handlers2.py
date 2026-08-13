from pathlib import Path
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

PE = Path(
    r"C:\Program Files (x86)\Steam\steamapps\common"
    r"\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"
)
data = PE.read_bytes()
e = struct.unpack_from("<I", data, 0x3C)[0]
num = struct.unpack_from("<H", data, e + 6)[0]
sz = struct.unpack_from("<H", data, e + 20)[0]
ib = struct.unpack_from("<Q", data, e + 24 + 24)[0]
sec = e + 24 + sz
secs = []
for i in range(num):
    o = sec + i * 40
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, o + 8)
    secs.append((vaddr, max(vsize, rsize), raddr))


def rva_to_off(rva: int):
    for v, s, r in secs:
        if v <= rva < v + s:
            return rva - v + r
    return None


md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True


def resolve(addr: int, raw: bytes):
    if len(raw) >= 7 and raw[0] == 0x48 and raw[1] == 0x8D and (raw[2] & 0xC7) == 0x05:
        disp = struct.unpack_from("<i", raw, 3)[0]
        t = addr + 7 + disp
        off = rva_to_off(t - ib)
        if off is None:
            return None
        end = data.find(b"\x00", off, off + 64)
        return data[off:end].decode("ascii", errors="replace")
    return None


for rva in [0xC0BA00, 0xD30B10]:
    off = rva_to_off(rva)
    print(f"\n#### {rva:#x}")
    for insn in md.disasm(data[off : off + 0x130], ib + rva):
        s = resolve(insn.address, bytes(insn.bytes))
        extra = f"  ; {s!r}" if s else ""
        print(f"  {insn.address:#x}  {insn.mnemonic} {insn.op_str}{extra}")
