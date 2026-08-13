import ida_auto
import ida_funcs
import ida_lines
import ida_name
import idc


def main():
    ida_auto.auto_wait()
    args = list(idc.ARGV[1:])
    if len(args) < 2:
        raise RuntimeError("expected output and address")
    output_path = args[0]
    address = int(args[1], 0)
    function = ida_funcs.get_func(address)
    if function is None:
        raise RuntimeError("no function")
    with open(output_path, "w", encoding="utf-8") as output:
        output.write(f"{ida_name.get_name(function.start_ea)} [{function.start_ea:#x}, {function.end_ea:#x})\n")
        ea = function.start_ea
        while ea != idc.BADADDR and ea < function.end_ea:
            raw = idc.get_bytes(ea, idc.get_item_size(ea)) or b""
            line = idc.generate_disasm_line(ea, 0) or ""
            output.write(f"{ea:#010x}: {raw.hex(' '):<28} {line}\n")
            next_ea = idc.next_head(ea, function.end_ea)
            if next_ea == idc.BADADDR or next_ea <= ea:
                break
            ea = next_ea
    idc.qexit(0)


if __name__ == "__main__":
    main()
