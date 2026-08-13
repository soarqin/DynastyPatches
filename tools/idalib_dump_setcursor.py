import sys
import idapro
import ida_auto
import ida_bytes
import ida_funcs
import ida_lines
import idc

db = r"E:\Games\Dynasty\Castle\exe\RPG.exe.i64"
out_path = r"E:\Projects\CastlePatches\analysis\idalib_setcursor.txt"
idapro.open_database(db, True)
ida_auto.auto_wait()
with open(out_path, "w", encoding="utf-8") as out:
    for address in (0x0043DF30, 0x00408830, 0x004044F0, 0x00404600):
        f = ida_funcs.get_func(address)
        out.write(f"\n=== {address:#010x} ===\n")
        if not f:
            out.write("no function\n")
            continue
        out.write(f"function {f.start_ea:#010x}-{f.end_ea:#010x}\n")
        ea = f.start_ea
        while ea < f.end_ea:
            size = idc.get_item_size(ea) or 1
            raw = ida_bytes.get_bytes(ea, size) or b""
            line = ida_lines.tag_remove(idc.generate_disasm_line(ea, 0) or "")
            out.write(f"{ea:#010x}: {raw.hex(' '):<30} {line}\n")
            ea += size
idc.qexit(0)
