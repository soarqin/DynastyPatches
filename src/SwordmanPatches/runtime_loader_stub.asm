BITS 32
%include "runtime_loader_stub.inc"
%define RETRIES 400
%define SPIN 10000h

    push ebp
    mov ebp, esp
    push ebx
    push esi
    mov ebx, [ebp+8]
    mov esi, RETRIES
.retry:
    mov eax, [ebx+STUB_GETMODULE_PTR]
    test eax, eax
    jz .wait
    mov eax, [eax]
    test eax, eax
    jz .wait
    cmp eax, -1
    je .wait
    push dword STUB_KERNEL_OFFSET
    add dword [esp], ebx
    call eax
    test eax, eax
    jz .wait
    mov edx, eax
    mov eax, [ebx+STUB_GETPROC_PTR]
    test eax, eax
    jz .wait
    mov eax, [eax]
    test eax, eax
    jz .wait
    cmp eax, -1
    je .wait
    push dword STUB_LOAD_OFFSET
    add dword [esp], ebx
    push edx
    call eax
    test eax, eax
    jz .wait
    push dword STUB_PATH_OFFSET
    add dword [esp], ebx
    call eax
    test eax, eax
    jz .wait
    mov edx, eax
    mov eax, [ebx+STUB_GETPROC_PTR]
    test eax, eax
    jz .wait
    mov eax, [eax]
    test eax, eax
    jz .wait
    cmp eax, -1
    je .wait
    push dword STUB_START_OFFSET
    add dword [esp], ebx
    push edx
    call eax
    test eax, eax
    jz .done
    push dword STUB_OPTIONS_OFFSET
    add dword [esp], ebx
    call eax
.done:
    pop esi
    pop ebx
    pop ebp
    ret 4
.wait:
    dec esi
    jz .failed
    pause
    mov ecx, SPIN
.spin:
    pause
    dec ecx
    jnz .spin
    jmp .retry
.failed:
    xor eax, eax
    jmp .done
