"""Export Castle map/render evidence through Python idapro only.

Usage: python tools/ida_castle_widescreen_backdrop.py [RPG.exe] [VA ...]
The input image is copied to build/ida-castle-backdrop before analysis.
"""

import hashlib
import os
from pathlib import Path
import shutil
import sys
import winreg


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EXE = Path(r"E:\Games\Dynasty\Castle\exe\RPG.exe")
TARGETS = (
    0x0040B050, 0x0040B0B0, 0x0040ADA0, 0x0040ADE0,
    0x00405A10, 0x00406330, 0x004064E0, 0x00406230, 0x004062E0,
    0x00434830, 0x00434710, 0x0040FBD0, 0x00431170,
    0x004035A0, 0x00409A50, 0x004040B0,
    0x0044B500, 0x0044B590, 0x00406DA0, 0x00403E30, 0x00407020,
    0x00405BD0, 0x0044CBD3, 0x00409BE0, 0x00409C50,
    0x0040AE80, 0x0040AEB0, 0x0040B130, 0x0040B340, 0x0040B3A0,
    0x0040AF10, 0x0040B200, 0x0040CBF0,
)


def find_ida():
    uninstall = r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"
    for hive in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
        for view in (winreg.KEY_WOW64_64KEY, winreg.KEY_WOW64_32KEY):
            try:
                with winreg.OpenKey(hive, uninstall, 0, winreg.KEY_READ | view) as key:
                    for index in range(winreg.QueryInfoKey(key)[0]):
                        with winreg.OpenKey(key, winreg.EnumKey(key, index)) as entry:
                            try:
                                name = winreg.QueryValueEx(entry, "DisplayName")[0]
                                location = winreg.QueryValueEx(entry, "InstallLocation")[0]
                            except OSError:
                                continue
                            if "IDA" in name and "9.4" in name and (Path(location) / "idalib.dll").is_file():
                                return location
            except OSError:
                continue
    raise RuntimeError("IDA installation was not found in the Windows registry")


def main():
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_EXE
    targets = tuple(int(value, 0) for value in sys.argv[2:]) or TARGETS
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    scratch = ROOT / "build" / "ida-castle-backdrop" / digest[:16]
    scratch.mkdir(parents=True, exist_ok=True)
    image = scratch / source.name
    if not image.exists():
        shutil.copy2(source, image)
    if hashlib.sha256(image.read_bytes()).hexdigest() != digest:
        raise RuntimeError(f"Analysis copy does not match the source: {image}")
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
        output_path = ROOT / "analysis" / "castle_widescreen_backdrop_ida.txt"
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("w", encoding="utf-8") as output:
            output.write(f"SOURCE {source}\nSHA256 {digest}\n")
            output.write(f"IDA {idapro.get_library_version()} via Python idapro\n")
            output.write(f"INPUT {ida_nalt.get_input_file_path()}\n")
            for address in targets:
                function = ida_funcs.get_func(address)
                output.write(f"\nTARGET {address:#010x} {idc.get_name(address)}\n")
                raw = ida_bytes.get_bytes(address, 24)
                output.write(f"BYTES {raw.hex(' ')}\n")
                for ref in idautils.XrefsTo(address):
                    owner = ida_funcs.get_func(ref.frm)
                    owner_name = idc.get_name(owner.start_ea) if owner else "<none>"
                    output.write(f"XREF {ref.frm:#010x} {owner_name}\n")
                if function is None:
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
                    disasm = ida_lines.tag_remove(idc.generate_disasm_line(ea, 0) or "")
                    output.write(f"{ea:#010x}: {raw:<44} {disasm}\n")
        print(output_path)
    finally:
        idapro.close_database(True)
    if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
        raise RuntimeError("The source image changed during analysis")


if __name__ == "__main__":
    main()
