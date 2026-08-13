"""Export surrounding disassembly for selected 32-bit PE virtual addresses.

Usage:
  idat -A -S"ida_dump.py <output-file> <hex-va> ..." RPG.exe
"""

import sys

import ida_auto
import ida_bytes
import ida_funcs
import ida_name
import idc


def main():
    ida_auto.auto_wait()
    args = list(idc.ARGV[1:])
    if len(args) < 2:
        raise RuntimeError("Expected an output path and at least one virtual address")

    output_path = args[0]
    addresses = [int(value, 0) for value in args[1:]]
    with open(output_path, "w", encoding="utf-8") as output:
        for address in addresses:
            function = ida_funcs.get_func(address)
            output.write("\n" + "=" * 80 + "\n")
            output.write(f"Requested VA: {address:#010x}\n")
            if function is None:
                output.write("No function identified at this location.\n")
                start = address - 32
                end = address + 64
            else:
                name = ida_name.get_name(function.start_ea) or "<unnamed>"
                output.write(
                    f"Function: {name} [{function.start_ea:#010x}, {function.end_ea:#010x})\n"
                )
                start = max(function.start_ea, address - 96)
                end = min(function.end_ea, address + 128)

            ea = start
            while ea != idc.BADADDR and ea < end:
                line = idc.generate_disasm_line(ea, 0) or ""
                raw = ida_bytes.get_bytes(ea, idc.get_item_size(ea)) or b""
                marker = " <=" if ea == address else ""
                output.write(f"{ea:#010x}: {raw.hex(' '):<28} {line}{marker}\n")
                next_ea = idc.next_head(ea, end)
                if next_ea == idc.BADADDR or next_ea <= ea:
                    break
                ea = next_ea

    idc.qexit(0)


if __name__ == "__main__":
    main()
