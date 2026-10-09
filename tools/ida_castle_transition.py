"""Export Castle fade/scene evidence using Python idapro, without running RPG.exe.

Usage: python tools/ida_castle_transition.py [RPG.exe] [VA ...]
"""

import hashlib
import os
from pathlib import Path
import re
import shutil
import sys

from ida_castle_widescreen_backdrop import find_ida

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EXE = Path(r"E:\Games\Dynasty\Castle\exe\RPG.exe")
TARGETS = (
    0x00406F20, 0x00406EE0, 0x00406BF0, 0x00406DA0,
    0x0040B230, 0x0040B2D0, 0x0040B3B0, 0x004035A0, 0x0040B150, 0x0040B050,
    0x0040CBF0, 0x0040D120, 0x0040D200, 0x0040D510, 0x0040D780,
    0x0040D8A0, 0x0040D9C0, 0x0040DAA0, 0x0040F4A0,
    0x0040B0A0, 0x0044B150, 0x0044B1F0,
    0x00413FA0, 0x004143F0, 0x00413570, 0x00413780, 0x00413B00, 0x00413CD0,
    0x00434920, 0x00435620, 0x00434820, 0x00434830,
    0x00432090, 0x00432130, 0x004322C0, 0x00432790, 0x004328A0,
    0x0044A110, 0x0044A1F0, 0x0044A2F0, 0x0040B0B0, 0x0040B120,
    0x0040AF10, 0x0040AF40, 0x0040AE80, 0x0040B340, 0x0040B200,
    0x0042CF50, 0x0042CF70, 0x0042D030, 0x0042D0E0, 0x0044E5CA,
    0x0040DF20, 0x0040DF90, 0x0040E000, 0x0040E060, 0x0040E0F0, 0x0040E120,
    0x0040E170, 0x0040E1B0, 0x0040E230, 0x0040ED70, 0x0040EDB0, 0x0043B510,
    0x0044A970, 0x00434710, 0x004345F0, 0x00405A10, 0x00431040,
    0x0043EE90, 0x00436D80, 0x004157A0, 0x00415710, 0x00436340,
    0x00431140, 0x00431170, 0x0043E740, 0x0043E940, 0x0043EDC0, 0x0043E9A0,
    0x004290A0, 0x00429480, 0x0043ECE0,
    0x00426520, 0x00426600, 0x00426640, 0x00426650,
)
VTABLES = (0x004611E4, 0x00460CB8, 0x00460298, 0x00460810, 0x00460E24, 0x00460BB0)
GLOBALS = (0x00468AE8, 0x0089F7A0, 0x0089F7A2, 0x0089F7A4,
           0x0089F7AC, 0x0089F7B4, 0x0089F808, 0x0089F810,
           0x0089F818, 0x0089F7D0, 0x0089F804, 0x00468AEC, 0x0089F81E,
           0x00468AEE, 0x00468AF0, 0x008E2428, 0x008B01F0, 0x0089FCD4, 0x008DED0C)


def main():
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_EXE
    targets = (*TARGETS, *(int(value, 0) for value in sys.argv[2:]))
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    scratch = ROOT / "build" / "ida-castle-backdrop" / digest[:16]
    scratch.mkdir(parents=True, exist_ok=True)
    image = scratch / source.name
    if not image.exists():
        shutil.copy2(source, image)
    if hashlib.sha256(image.read_bytes()).hexdigest() != digest:
        raise RuntimeError(f"Analysis copy does not match: {image}")
    os.environ["IDADIR"] = find_ida()
    import idapro

    if idapro.open_database(str(image), True) != 0:
        raise RuntimeError(f"Cannot open analysis copy: {image}")
    try:
        import ida_auto
        import ida_bytes
        import ida_funcs
        import ida_hexrays
        import ida_lines
        import ida_nalt
        import ida_ua
        import idautils
        import idc

        ida_auto.auto_wait()
        # IDA may leave these vtable entry points as code outside a function.
        # Define only the boundaries checked in the disassembly, in the scratch database.
        for start, end in ((0x0044A2F0, 0x0044A306), (0x0042CF50, 0x0042CF70),
                           (0x00431140, 0x00431161),
                           (0x0040B150, 0x0040B1BC), (0x004328A0, 0x00432A1F)):
            if ida_funcs.get_func(start) is None:
                ida_funcs.add_func(start, end)
        ida_auto.auto_wait()
        factory = str(ida_hexrays.decompile(0x0040B3B0))
        opcode_path = ROOT / "analysis" / "castle_transition_opcodes.txt"
        with opcode_path.open("w", encoding="utf-8") as output:
            for match in re.finditer(r"case (0x[0-9A-Fa-f]+|[0-9]+)u?:(.*?)(?=case |default:)", factory, re.S):
                opcode = int(match[1], 0)
                table = re.search(r"&off_([0-9A-Fa-f]+)", match[2])
                if table is None:
                    output.write(f"OPCODE {opcode:#04x} {match[2].strip()}\n")
                    continue
                vtable = int(table[1], 16)
                handlers = [ida_bytes.get_dword(vtable + offset) for offset in (0, 4, 8)]
                names = " ".join(f"{ea:#010x}={idc.get_name(ea)}" for ea in handlers)
                output.write(f"OPCODE {opcode:#04x} VTABLE {vtable:#010x} {names}\n")
        functions = set(targets)
        functions.update(idautils.Functions(0x0044A000, 0x0044A320))
        functions.update(idautils.Functions(0x00432890, 0x00432D00))
        functions.update(idautils.Functions(0x0042D030, 0x0042D230))
        output_path = ROOT / "analysis" / "castle_transition_ida.txt"
        with output_path.open("w", encoding="utf-8") as output:
            output.write(f"SOURCE {source}\nSHA256 {digest}\n")
            output.write(f"IDA {idapro.get_library_version()} via Python idapro\n")
            output.write(f"INPUT {ida_nalt.get_input_file_path()}\n")
            output.write("\nPATCH ENTRY BYTES\n")
            for address in (0x00468AEE, 0x00468AF0):
                output.write(f"INITIAL {address:#010x} {ida_bytes.get_word(address)}\n")
            for address, size in ((0x00406DA0, 6), (0x00406F20, 8), (0x00406EE0, 8),
                                  (0x0040B3B0, 7), (0x0040AF40, 7), (0x0040B0B0, 6), (0x0040B0A0, 8),
                                  (0x004143F0, 7), (0x00435620, 7), (0x00431170, 9), (0x00429480, 9),
                                  (0x0043B510, 8)):
                raw = ida_bytes.get_bytes(address, size)
                output.write(f"{address:#010x} {raw.hex(' ')}\n")
            for table in VTABLES:
                output.write(f"\nVTABLE {table:#010x}\n")
                for offset in range(0, 48, 4):
                    address = ida_bytes.get_dword(table + offset)
                    output.write(f"{offset:#04x}: {address:#010x} {idc.get_name(address)}\n")
                    if ida_funcs.get_func(address) is not None:
                        functions.add(address)
            for target in (0x00406F20, 0x00406EE0, 0x00406DA0, 0x00406BF0, *GLOBALS):
                output.write(f"\nXREFS {target:#010x} {idc.get_name(target)}\n")
                for ref in idautils.XrefsTo(target):
                    owner = ida_funcs.get_func(ref.frm)
                    name = idc.get_name(owner.start_ea) if owner else "<none>"
                    text = ida_lines.tag_remove(idc.generate_disasm_line(ref.frm, 0) or "")
                    output.write(f"{ref.frm:#010x} {name}: {text}\n")
                    if owner is not None:
                        functions.add(owner.start_ea)
            for address in sorted(functions):
                function = ida_funcs.get_func(address)
                if function is None:
                    function = ida_funcs.get_func(address + 1)
                output.write(f"\nFUNCTION {address:#010x} {idc.get_name(address)}\n")
                for ref in idautils.XrefsTo(address):
                    owner = ida_funcs.get_func(ref.frm)
                    name = idc.get_name(owner.start_ea) if owner else "<data>"
                    output.write(f"CALLER {ref.frm:#010x} {name}\n")
                raw = ida_bytes.get_bytes(address, 24)
                output.write(f"BYTES {raw.hex(' ')}\n")
                if function is None:
                    output.write("FUNCTION NOT FOUND\n")
                    ea = address
                    for _ in range(80):
                        instruction = ida_ua.insn_t()
                        size = ida_ua.decode_insn(instruction, ea)
                        if size <= 0:
                            break
                        raw = ida_bytes.get_bytes(ea, size).hex(" ")
                        text = ida_lines.tag_remove(idc.generate_disasm_line(ea, 0) or "")
                        output.write(f"{ea:#010x}: {raw:<44} {text}\n")
                        ea += size
                    continue
                try:
                    output.write(str(ida_hexrays.decompile(function.start_ea)) + "\n")
                except Exception as error:
                    output.write(f"DECOMPILE ERROR {error}\n")
                for ea in idautils.FuncItems(function.start_ea):
                    instruction = ida_ua.insn_t()
                    size = ida_ua.decode_insn(instruction, ea)
                    if size <= 0:
                        continue
                    raw = ida_bytes.get_bytes(ea, size).hex(" ")
                    text = ida_lines.tag_remove(idc.generate_disasm_line(ea, 0) or "")
                    output.write(f"{ea:#010x}: {raw:<44} {text}\n")
        print(output_path)
    finally:
        idapro.close_database(True)
    if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
        raise RuntimeError("The source image changed during analysis")


if __name__ == "__main__":
    main()
