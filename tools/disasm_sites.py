from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import struct
from pathlib import Path

PE = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\engine2.dll")
IMAGE_BASE = 0x180000000
data = PE.read_bytes()
e = struct.unpack_from("<I", data, 0x3C)[0]
num = struct.unpack_from("<H", data, e + 6)[0]
sz = struct.unpack_from("<H", data, e + 20)[0]
sec = e + 24 + sz
secs = []
for i in range(num):
    o = sec + i * 40
    name = data[o:o+8].split(b"\0",1)[0].decode()
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, o+8)
    secs.append((vaddr, max(vsize,rsize), raddr))

def off(rva):
    for vaddr, span, raddr in secs:
        if vaddr <= rva < vaddr+span:
            return rva - vaddr + raddr
    raise SystemExit(f"bad {rva:#x}")

md = Cs(CS_ARCH_X86, CS_MODE_64)
for rva, n in [(0x75eb0, 40), (0x7b620, 40), (0x6a900, 50)]:
    print(f"\n=== {IMAGE_BASE+rva:#x} (RVA {rva:#x}) ===")
    blob = data[off(rva):off(rva)+n*8]
    for insn in md.disasm(blob, IMAGE_BASE+rva):
        print(f"  {insn.address:#x}  {insn.mnemonic} {insn.op_str}")
        if insn.address > IMAGE_BASE + rva + n*3:
            break
