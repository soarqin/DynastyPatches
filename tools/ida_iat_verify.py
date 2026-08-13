import ida_auto
import ida_bytes
import ida_lines
import ida_name
import idc

OUTPUT = r"E:\Projects\CastlePatches\analysis\idalib_iat_verify.txt"

def dump(out, ea):
    raw = ida_bytes.get_bytes(ea, 4) or b""
    text = ida_lines.tag_remove(idc.generate_disasm_line(ea, 0) or "")
    out.write(f"{ea:#010x}: {raw.hex(' '):<11} {text} name={ida_name.get_name(ea)!r}\n")

ida_auto.auto_wait()
with open(OUTPUT, "w", encoding="utf-8") as out:
    out.write("IDA/idalib IAT verification\n")
    for ea in (0x00460090, 0x004600C8, 0x0046019C, 0x004601EC, 0x004601F0, 0x00460204):
        dump(out, ea)
idc.qexit(0)
