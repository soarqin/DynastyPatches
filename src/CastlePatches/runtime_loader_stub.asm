; Remote loader stub for CastleRuntime.dll.
;
; This code runs as a CreateRemoteThread thread routine inside the suspended
; 32-bit game process. The launcher assembles it to a flat binary at build
; time (nasm -f bin, see src/CastlePatches/CMakeLists.txt) and embeds the
; bytes as a generated C array, so launchers of either bitness share it.
;
; Entry: WINAPI thread routine (stdcall). [ebp+8] receives the base address
; of the remote data block passed as lpParameter; see the STUB_* offsets in
; runtime_loader_stub.inc for its layout (strings are written by the launcher).
; Exit: eax is the CastleRuntimeStart return value (1 = success), or 0 when
; resolution fails or the bounded retry loop is exhausted.

BITS 32

%include "runtime_loader_stub.inc"

%define RETRY_COUNT 400
%define SPIN_COUNT  0x00010000

    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov ebx, [ebp+8]        ; ebx = data block base (lpParameter)
    mov esi, RETRY_COUNT    ; esi = bounded retry counter

retry_loop:
    ; The target's import slots can still be zero/FFFFFFFF while its
    ; suspended loader is finishing. Check each slot before calling through
    ; it; jumping through FFFFFFFF was the original startup crash.
    mov eax, [GET_MODULE_HANDLE_IAT_ADDRESS]
    cmp eax, -1
    je retry
    test eax, eax
    je retry
    lea ecx, [ebx + STUB_KERNEL_NAME_OFFSET]
    push ecx
    call eax                ; GetModuleHandleA("kernel32.dll")
    test eax, eax
    je retry
    mov edx, eax            ; edx = kernel32 module handle

    ; Resolve GetProcAddress before pushing its arguments so every retry
    ; path leaves the stack balanced.
    mov eax, [GET_PROC_ADDRESS_IAT_ADDRESS]
    cmp eax, -1
    je retry
    test eax, eax
    je retry
    lea ecx, [ebx + STUB_LOAD_NAME_OFFSET]
    push ecx
    push edx
    call eax                ; GetProcAddress(kernel32, "LoadLibraryW")
    test eax, eax
    je retry

    lea ecx, [ebx + STUB_PATH_OFFSET]
    push ecx
    call eax                ; LoadLibraryW(CastleRuntime.dll path)
    test eax, eax
    je retry
    mov edx, eax            ; edx = CastleRuntime module handle

    ; Resolve GetProcAddress again after LoadLibraryW returned.
    mov eax, [GET_PROC_ADDRESS_IAT_ADDRESS]
    cmp eax, -1
    je retry
    test eax, eax
    je retry
    lea ecx, [ebx + STUB_START_NAME_OFFSET]
    push ecx
    push edx
    call eax                ; GetProcAddress(CastleRuntime, "CastleRuntimeStart")
    test eax, eax
    je finish               ; Missing export: skip only the call, eax stays 0.
    call eax                ; CastleRuntimeStart()

finish:
    pop esi
    pop ebx
    pop ebp
    ret 4                   ; stdcall: callee discards lpParameter

retry:
    ; Never call Sleep through the target IAT here: this thread can run
    ; while the process is still completing loader initialization.
    ; A PAUSE-backed bounded spin is self-contained.
    dec esi
    jz exhausted
    pause
    mov ecx, SPIN_COUNT
spin_inner:
    pause
    dec ecx
    jnz spin_inner
    jmp retry_loop

exhausted:
    xor eax, eax
    pop esi
    pop ebx
    pop ebp
    ret 4
