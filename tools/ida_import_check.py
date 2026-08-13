import ida_auto
import ida_bytes
import ida_lines
import ida_name
import idaapi
import idc

OUTPUT = r"E:\Projects\CastlePatches\analysis\idalib_import_check.txt"

def line(ea):
    raw = ida_bytes.get_bytes(ea, 8) or b""
    text = ida_lines.tag_remove(idc.generate_disasm_line(ea, 0) or "")
    return f"{ea:#010x}: {raw.hex(' '):<23} {text} name={ida_name.get_name(ea)!r}\n"

def main():
    ida_auto.auto_wait()
    with open(OUTPUT, "w", encoding="utf-8") as out:
        out.write("IDA/idalib import address verification\n")
        for ea in (0x0046019C, 0x004601EC, 0x004601F0, 0x00460204):
            out.write(line(ea))
        out.write("\nKnown callers from IDA database:\n")
        for ea in (0x004044F9, 0x00404608, 0x00406531, 0x0040653D, 0x0040654B):
            out.write(line(ea))
        out.write("\nImports:\n")
        for i in range(idaapi.get_import_module_qty()):
            module = idaapi.get_import_module_name(i) or "<unknown>"
            out.write(f"[{module}]\n")
            def callback(ea, name, ordinal):
                if ea in (0x0046019C, 0x004601EC, 0x004601F0, 0x00460204):
                    out.write(f"  {ea:#010x}: {name!r} ordinal={ordinal}\n")
                return True
            idaapi.enum_import_names(i, callback)

main()
