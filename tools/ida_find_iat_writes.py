import ida_auto
import ida_bytes
import ida_funcs
import ida_lines
import ida_name
import ida_xref
import idautils
import idc

TARGET = 0x00460204
OUTPUT = r"E:\Projects\CastlePatches\analysis\ida_iat_writes.txt"

ida_auto.auto_wait()
with open(OUTPUT, "w", encoding="utf-8") as out:
    out.write(f"IAT write audit for {TARGET:#010x}\n")
    out.write("DataRefsTo:\n")
    for x in idautils.XrefsTo(TARGET, 0):
        out.write(f"  {x.frm:#010x} type={x.type:#x} {ida_lines.tag_remove(idc.generate_disasm_line(x.frm, 0) or '')}\n")

    out.write("\nImmediate operand matches:\n")
    for func in idautils.Functions():
        ea = func
        end = ida_funcs.get_func(ea).end_ea
        while ea < end:
            size = idc.get_item_size(ea)
            if size <= 0:
                size = 1
            for opnum in range(8):
                try:
                    value = idc.get_operand_value(ea, opnum)
                except Exception:
                    continue
                if value == TARGET:
                    line = ida_lines.tag_remove(idc.generate_disasm_line(ea, 0) or "")
                    out.write(f"  {ea:#010x} op{opnum}: {line}\n")
            ea += size
    out.write("\nAll xrefs:\n")
    for x in idautils.XrefsTo(TARGET, 1):
        out.write(f"  {x.frm:#010x} type={x.type:#x} {ida_lines.tag_remove(idc.generate_disasm_line(x.frm, 0) or '')}\n")

idc.qexit(0)
