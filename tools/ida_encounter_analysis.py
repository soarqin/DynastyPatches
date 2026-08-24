"""Export focused IDA evidence for Castle random-encounter analysis.

Usage:
  python ida_encounter_analysis.py <RPG.exe.i64> <output-file>
"""

import sys

import idapro


FUNCTIONS = (
    0x004034C0,
    0x00403510,
    0x00403590,
    0x004035A0,
    0x00408C40,
    0x00408E80,
    0x0040CBF0,
    0x0040AC00,
    0x0040ADA0,
    0x0044B0D0,
)

RANGES = (
    (0x004093AA, 0x004093DA, "map record encounter setup"),
    (0x0040B150, 0x0040B166, "map update encounter poll"),
)


def main():
    if len(sys.argv) != 3:
        raise RuntimeError("expected IDA database and output path")

    database, output_path = sys.argv[1:]
    result = idapro.open_database(database, True)
    if result != 0:
        raise RuntimeError(f"failed to open IDA database: error {result}")

    import ida_auto
    import ida_bytes
    import ida_funcs
    import ida_hexrays
    import ida_lines
    import ida_name
    import idautils
    import idc

    ida_auto.auto_wait()

    def instruction(ea):
        size = idc.get_item_size(ea) or 1
        raw = ida_bytes.get_bytes(ea, size) or b""
        text = ida_lines.tag_remove(idc.generate_disasm_line(ea, 0) or "")
        return f"{ea:#010x}: {raw.hex(' '):<30} {text}\n"

    def dump_range(output, start, end):
        ea = start
        while ea != idc.BADADDR and ea < end:
            output.write(instruction(ea))
            next_ea = idc.next_head(ea, end)
            if next_ea == idc.BADADDR or next_ea <= ea:
                break
            ea = next_ea

    def get_function(address):
        function = ida_funcs.func_entry_info_t()
        if not ida_funcs.get_func_entry_info(function, address):
            return None
        return function

    with open(output_path, "w", encoding="utf-8") as output:
        for address in FUNCTIONS:
            function = get_function(address)
            if function is None:
                raise RuntimeError(f"no function at {address:#010x}")
            name = ida_name.get_name(function.start_ea) or "<unnamed>"
            output.write("\n" + "=" * 100 + "\n")
            output.write(
                f"Function {name} [{function.start_ea:#010x}, {function.end_ea:#010x})\n"
            )
            dump_range(output, function.start_ea, function.end_ea)

            output.write("Callers:\n")
            for xref in idautils.XrefsTo(function.start_ea, 0):
                caller = get_function(xref.frm)
                if caller is None:
                    output.write(f"  {xref.frm:#010x}: no containing function\n")
                    continue
                caller_name = ida_name.get_name(caller.start_ea) or "<unnamed>"
                output.write(
                    f"  {xref.frm:#010x} in {caller_name} "
                    f"[{caller.start_ea:#010x}, {caller.end_ea:#010x})\n"
                )

            output.write("Pseudocode:\n")
            try:
                output.write(str(ida_hexrays.decompile(function.start_ea)) + "\n")
            except Exception as error:
                output.write(f"  unavailable: {error}\n")

        output.write("\n" + "=" * 100 + "\n")
        output.write("Selected ranges\n")
        for start, end, label in RANGES:
            output.write(f"\n{label} [{start:#010x}, {end:#010x})\n")
            dump_range(output, start, end)

    idapro.close_database(False)


if __name__ == "__main__":
    main()
