from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import struct

pe_path = r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\engine2.dll"
data = open(pe_path, "rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
num_sec = struct.unpack_from("<H", data, e_lfanew + 6)[0]
size_opt = struct.unpack_from("<H", data, e_lfanew + 20)[0]
sec_off = e_lfanew + 24 + size_opt
sections = []
for i in range(num_sec):
    off = sec_off + i * 40
    name = data[off : off + 8].split(b"\0", 1)[0].decode()
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", data, off + 8)
    sections.append((name, vaddr, vsize, raddr, rsize))


def rva_to_off(rva: int) -> int | None:
    for name, vaddr, vsize, raddr, rsize in sections:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return rva - vaddr + raddr
    return None


base = 0x180000000
start_rva = 0x4D860
off = rva_to_off(start_rva)
assert off is not None
blob = data[off : off + 0x120]
md = Cs(CS_ARCH_X86, CS_MODE_64)
print(f"Function @ {base+start_rva:#x} (RVA {start_rva:#x})")
for insn in md.disasm(blob, base + start_rva):
    print(f"  {insn.address:#x}  {insn.bytes.hex():32}  {insn.mnemonic} {insn.op_str}")
    if insn.address > base + start_rva + 0x90:
        break
