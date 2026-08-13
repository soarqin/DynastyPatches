import ida_auto
import ida_bytes
import ida_funcs
import ida_name
import idc


def main():
    ida_auto.auto_wait()
    args = list(idc.ARGV[1:])
    if len(args) < 2:
        raise RuntimeError("expected output and hex offset")
    output_path = args[0]
    needle = args[1].lower().replace("0x", "")
    with open(output_path, "w", encoding="utf-8") as output:
        for head in idc.Heads(0, ida_bytes.get_inf_structure().max_ea):
            text = idc.generate_disasm_line(head, 0) or ""
            if needle not in text.lower().replace("h", ""):
                continue
            function = ida_funcs.get_func(head)
            name = ida_name.get_name(function.start_ea) if function else "<none>"
            output.write(f"{head:#010x} {name}: {text}\n")
    idc.qexit(0)


if __name__ == "__main__":
    main()
