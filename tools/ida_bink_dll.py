import ida_auto, ida_name, ida_funcs, ida_nalt, idc
ida_auto.auto_wait()
for ea in range(0,0x100000,1):
 n=ida_name.get_name(ea)
 if n and 'BinkCopyToBuffer' in n:
  print('FOUND',hex(ea),n, ida_funcs.get_func(ea))
  f=ida_funcs.get_func(ea)
  if f:
   x=f.start_ea
   while x<f.end_ea:
    print(hex(x), idc.get_bytes(x,idc.get_item_size(x)).hex(), idc.generate_disasm_line(x,0))
    x=idc.next_head(x,f.end_ea)
ida_pro.qexit(0)
