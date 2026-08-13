import ida_auto
import ida_bytes
import ida_funcs
import ida_lines
import ida_name
import idc

OUTPUT = r"E:\Projects\CastlePatches\analysis\idalib_verify_final.txt"

def dump_function(out, address):
    function = ida_funcs.get_func(address)
    out.write(f"Function request {address:#010x}\n")
    if function is None:
        out.write("  no function\n")
        return
    out.write(f"  {ida_name.get_name(function.start_ea)} [{function.start_ea:#010x}, {function.end_ea:#010x})\n")
    ea = function.start_ea
    while ea != idc.BADADDR and ea < function.end_ea:
        raw = ida_bytes.get_bytes(ea, idc.get_item_size(ea)) or b""
        text = ida_lines.tag_remove(idc.generate_disasm_line(ea, 0) or "")
        out.write(f"  {ea:#010x}: {raw.hex(' '):<28} {text}\n")
        nxt = idc.next_head(ea, function.end_ea)
        if nxt == idc.BADADDR or nxt <= ea:
            break
        ea = nxt

ida_auto.auto_wait()
with open(OUTPUT, "w", encoding="utf-8") as out:
    dump_function(out, 0x0043DF30)
    dump_function(out, 0x00408830)
idc.qexit(0)
