from pathlib import Path
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

pe = Path(r"C:\code\CS2-demo-anyskin\tmp-engine2.dll")
data = pe.read_bytes()
e = struct.unpack_from("<I", data, 0x3C)[0]
num = struct.unpack_from("<H", data, e + 6)[0]
sz = struct.unpack_from("<H", data, e + 20)[0]
ib = struct.unpack_from("<Q", data, e + 24 + 24)[0]
sec = e + 24 + sz
secs = []
for i in range(num):
    o = sec + i * 40
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, o + 8)
    secs.append((vaddr, max(vsize, rsize), raddr, rsize))
text_va, _, text_raw, text_rsize = secs[0]
blob = data[text_raw : text_raw + text_rsize]
target = 0x24800
calls = []
for i in range(len(blob) - 5):
    if blob[i] == 0xE8:
        rel = struct.unpack_from("<i", blob, i + 1)[0]
        abs_rva = text_va + i + 5 + rel
        if abs_rva == target:
            calls.append(text_va + i)
print("direct calls to setter", [hex(c) for c in calls])

pat = bytes([0xFF, 0x90, 0x18, 0x01, 0x00, 0x00])  # call [rax+0x118]
pat2 = bytes([0xFF, 0x91, 0x18, 0x01, 0x00, 0x00])  # call [rcx+0x118]
hits = []
start = 0
while True:
    i = blob.find(pat, start)
    if i < 0:
        break
    hits.append(text_va + i)
    start = i + 1
start = 0
while True:
    i = blob.find(pat2, start)
    if i < 0:
        break
    hits.append(text_va + i)
    start = i + 1
print("call [r+0x118] hits", len(hits), [hex(h) for h in hits[:30]])
md = Cs(CS_ARCH_X86, CS_MODE_64)
for rva in hits[:20]:
    o = text_raw + (rva - text_va)
    print(f"\n=== {rva:#x} ===")
    for insn in md.disasm(data[o - 0x28 : o + 0x0C], ib + rva - 0x28):
        mark = " <<" if insn.address == ib + rva else ""
        print(f"  {insn.address:#x} {insn.mnemonic} {insn.op_str}{mark}")
