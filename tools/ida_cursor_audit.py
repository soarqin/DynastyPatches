"""Export IDA's current view of mouse API callers and cursor state code.

Usage through IDA as Library:
  python idacli.py -f RPG.exe.i64 -s ida_cursor_audit.py -p false
"""

import ida_auto
import ida_bytes
import ida_funcs
import ida_lines
import ida_name
import idautils
import idc


OUTPUT = r"E:\Projects\CastlePatches\analysis\idalib_cursor_audit.txt"
TARGETS = (0x00460204, 0x0046019C, 0x0043DF30, 0x00408830)


def write_instruction(output, ea):
    raw = ida_bytes.get_bytes(ea, idc.get_item_size(ea)) or b""
    text = ida_lines.tag_remove(idc.generate_disasm_line(ea, 0) or "")
    output.write(f"  {ea:#010x}: {raw.hex(' '):<28} {text}\n")


def main():
    ida_auto.auto_wait()
    with open(OUTPUT, "w", encoding="utf-8") as output:
        for target in TARGETS:
            name = ida_name.get_name(target) or "<unnamed>"
            output.write(f"\n{'=' * 80}\n{target:#010x} {name}\n")
            for xref in idautils.XrefsTo(target, 0):
                function = ida_funcs.get_func(xref.frm)
                if function is None:
                    output.write(f"xref {xref.frm:#010x}: no function\n")
                    continue
                function_name = ida_name.get_name(function.start_ea) or "<unnamed>"
                output.write(
                    f"xref {xref.frm:#010x} in {function_name} "
                    f"[{function.start_ea:#010x}, {function.end_ea:#010x})\n"
                )
                ea = max(function.start_ea, xref.frm - 16)
                end = min(function.end_ea, xref.frm + 24)
                while ea != idc.BADADDR and ea < end:
                    write_instruction(output, ea)
                    ea = idc.next_head(ea, end)

        for address in (0x0043DF30, 0x00408830):
            function = ida_funcs.get_func(address)
            if function is None:
                continue
            output.write(f"\n{'=' * 80}\nFunction {address:#010x}\n")
            ea = function.start_ea
            while ea != idc.BADADDR and ea < function.end_ea:
                write_instruction(output, ea)
                ea = idc.next_head(ea, function.end_ea)


main()
