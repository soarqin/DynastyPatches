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

- Use IDA Pro 9.4 or idalib for executable analysis.
- Preserve analysis evidence under `analysis/` and helper scripts under `tools/`.
- Do not replace IDA-based analysis with `llvm-objdump` or similar low-fidelity tools.

## Runtime patching

- Patch only the newly created suspended target process; never modify the original game executable on disk.
- Keep runtime behavior inside the injected target-process module whenever it must continue after the launcher exits.
- Separate executable code allocations from writable data allocations.
- Validate patch sites before writing and choose jump encodings based on the actual displacement.
- Preserve diagnostic information when investigating crashes. Do not hide a crash by reverting a patch without identifying its cause.

## File hygiene

- Do not commit build directories, generated binaries, PDB files, IDE metadata, dumps, or local configuration files.
- Keep temporary reverse-engineering outputs out of the tracked source tree unless they are useful evidence.
- Use `apply_patch` for source edits where practical.

## Documentation

Project documentation is written in concise Chinese. Keep user-visible Chinese text accurate and readable; use straight technical terminology and avoid marketing language.
