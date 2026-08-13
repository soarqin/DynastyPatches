import ida_auto, ida_bytes, ida_lines, idc
out_path=r"E:\Projects\CastlePatches\analysis\idalib_cursor_bytes.txt"
ida_auto.auto_wait()
with open(out_path,"w",encoding="utf-8") as out:
    for start,end in ((0x43df20,0x43df90),(0x4044e0,0x404620),(0x408820,0x408900)):
        out.write(f"--- {start:#x}-{end:#x}\n")
        ea=start
        while ea<end:
            size=idc.get_item_size(ea)
            if size<=0: size=1
            raw=ida_bytes.get_bytes(ea,size) or b''
            txt=ida_lines.tag_remove(idc.generate_disasm_line(ea,0) or '')
            out.write(f"{ea:#010x}: {raw.hex(' '):<28} {txt}\n")
            ea += size
idc.qexit(0)
