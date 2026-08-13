"""Export code/data references to named imports or virtual addresses.

Usage:
  idat -A -S"ida_xrefs.py <output-file> <symbol-or-hex-va> ..." RPG.exe
"""

import idautils
import ida_auto
import ida_funcs
import ida_name
import idc


def resolve_target(value):
    try:
        return int(value, 0)
    except ValueError:
        address = ida_name.get_name_ea(idc.BADADDR, value)
        if address == idc.BADADDR:
            raise RuntimeError(f"Unknown name: {value}")
        return address


def write_instruction(output, ea):
    output.write(f"  {ea:#010x}: {idc.generate_disasm_line(ea, 0) or ''}\n")


def main():
    ida_auto.auto_wait()
    args = list(idc.ARGV[1:])
    if len(args) < 2:
        raise RuntimeError("Expected an output path and at least one target")

    with open(args[0], "w", encoding="utf-8") as output:
        for value in args[1:]:
            target = resolve_target(value)
            name = ida_name.get_name(target) or value
            output.write("\n" + "=" * 80 + "\n")
            output.write(f"Target {name} at {target:#010x}\n")
            refs = list(idautils.XrefsTo(target, 0))
            output.write(f"References: {len(refs)}\n")
            for ref in refs:
                source = ref.frm
                function = ida_funcs.get_func(source)
                if function is None:
                    context = "no containing function"
                else:
                    function_name = ida_name.get_name(function.start_ea) or "<unnamed>"
                    context = f"{function_name} [{function.start_ea:#010x}, {function.end_ea:#010x})"
                output.write(f"From {source:#010x} ({context})\n")
                previous = idc.prev_head(source, 0)
                if previous != idc.BADADDR:
                    write_instruction(output, previous)
                write_instruction(output, source)
                following = idc.next_head(source, idc.BADADDR)
                if following != idc.BADADDR:
                    write_instruction(output, following)

    idc.qexit(0)


if __name__ == "__main__":
    main()
