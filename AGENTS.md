# Agent Guide

## Repository scope

This repository contains runtime patch tools for classic Hantang games. The current tool is `CastlePatches` for the Castle game.

Keep each game's launcher and injected runtime under its own directory below `src/`. Shared build configuration belongs in the repository root or in clearly named CMake modules.

## Build and validation

- Use C11 and CMake.
- The Castle target process is 32-bit x86. Its injected runtime DLL must remain 32-bit even when the launcher is built as x64.
- After source or build-system changes, configure and build both Win32 and x64 launchers when the required Visual Studio generator is available.
- Do not launch the game automatically during validation.

## Reverse engineering

- Use IDA Pro 9.4 idat or idalib for executable analysis.
- On Windows: Search IDA Pro through registry installation entry.
- Preserve analysis evidence under `analysis/` and helper scripts under `tools/`.
- Do not replace IDA-based analysis with `llvm-objdump` or similar low-fidelity tools.

## Runtime patching

- Patch only the newly created suspended target process; never modify the original game executable on disk.
- Keep runtime behavior inside the injected target-process module whenever it must continue after the launcher exits.
- Separate executable code allocations from writable data allocations.
- Keep version-specific game addresses in the game's address table and validate every expected byte pattern before writing or installing a hook.
- Use MinHook for function hooks and imported API hooks (`MH_CreateHook` / `MH_CreateHookApi`); do not locate or overwrite game IAT slots by fixed address.
- Let MinHook create and relocate trampolines. Do not hand-write trampoline allocation, jump displacement, or runtime-generated machine-code buffers.
- Put injected x86 instruction blocks in standalone NASM sources; do not assemble instruction bytes in C. The runtime DLL links its NASM object directly; the launcher's remote loader stub is assembled at build time with `nasm -f bin` and embedded as a generated C array, so launchers of either bitness share the same code path.
- Preserve diagnostic information when investigating crashes. Do not hide a crash by reverting a patch without identifying its cause.

## Dependencies

- Add third-party libraries through CPM.cmake with a pinned version or commit.
- Credit every third-party library and its license in `README.md`.

## File hygiene

- Do not commit build directories, generated binaries, PDB files, IDE metadata, dumps, or local configuration files.
- Keep temporary reverse-engineering outputs out of the tracked source tree unless they are useful evidence.
- Use `apply_patch` for source edits where practical.

## Documentation

Project documentation is written in concise Chinese. Keep user-visible Chinese text accurate and readable; use straight technical terminology and avoid marketing language.
