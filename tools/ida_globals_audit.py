import ida_auto, ida_bytes, ida_funcs, ida_lines, ida_name, idautils, idc

OUT = r'E:\Projects\CastlePatches\analysis\idalib_globals_audit.txt'
TARGETS = (0x978514, 0x978518, 0x89F6C0, 0x89F7D0, 0x89F7D4, 0x46F384)

def line(ea):
    raw = ida_bytes.get_bytes(ea, idc.get_item_size(ea)) or b''
    txt = ida_lines.tag_remove(idc.generate_disasm_line(ea, 0) or '')
    return f'  {ea:#010x}: {raw.hex(" "):<30} {txt}\n'

ida_auto.auto_wait()
with open(OUT, 'w', encoding='utf-8') as f:
    for addr in (0x43DF30, 0x43DF50, 0x408830):
        fn=ida_funcs.get_func(addr)
        f.write(f'\nFUNCTION {addr:#010x} start={(fn.start_ea if fn else 0):#010x} end={(fn.end_ea if fn else 0):#010x}\n')
        if fn:
            ea=fn.start_ea
            while ea != idc.BADADDR and ea < fn.end_ea:
                f.write(line(ea)); ea=idc.next_head(ea,fn.end_ea)
    for target in TARGETS:
        f.write(f'\n==== {target:#010x} {ida_name.get_name(target) or ""} ====\n')
        for x in idautils.XrefsTo(target, 0):
            fn = ida_funcs.get_func(x.frm)
            f.write(f'xref {x.frm:#010x} fn {(fn.start_ea if fn else 0):#010x}\n')
            if fn:
                ea=max(fn.start_ea,x.frm-20); end=min(fn.end_ea,x.frm+28)
                while ea != idc.BADADDR and ea < end:
                    f.write(line(ea)); ea=idc.next_head(ea,end)
