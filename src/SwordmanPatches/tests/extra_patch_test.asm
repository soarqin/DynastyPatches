BITS 32
%define SWORDMAN_CAST_AFTER_MOVE_ALLOW _SwordmanTestCastAllow
%define SWORDMAN_CAST_AFTER_MOVE_CONTINUE _SwordmanTestCastContinue
%define SWORDMAN_SUMMON_ITEM_COPY_CONTINUE _SwordmanTestCopyContinue
%include "runtime/gameplay_hooks.asm"

global _SwordmanExtraPatchProbe
section .text

_SwordmanExtraPatchProbe:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    sub esp, 20h
    mov edi, [ebp+8]
    mov ecx, [ebp+0Ch]
    mov ebx, [ebp+10h]
    mov eax, esp
    mov esi, 0x24681357
    mov edx, 0x13572468
    test ecx, ecx
    jnz .cast
    call _SwordmanCastAfterMoveHook
    jmp _SwordmanTestProbeCastDone
.cast:
    call _SwordmanCastAfterMoveHook
_SwordmanTestProbeCastDone:
    mov [edi+4], eax
    mov [edi+8], ecx
    mov [edi+0Ch], edx
    mov [edi+10h], esi
    pushfd
    pop dword [edi+14h]
    cmp ebx, 0
    je _SwordmanTestProbeDone
    mov dword [edi], 3
    mov eax, esp
    mov esi, 0x24681357
    mov edx, 0x13572468
    xor ecx, ecx
    call _SwordmanSummonItemCopyHook
    jmp _SwordmanTestProbeCopyDone

_SwordmanTestCastContinue:
    lea esp, [esp+4]
    mov dword [edi], 1
    jmp _SwordmanTestProbeCastDone

_SwordmanTestCastAllow:
    lea esp, [esp+4]
    mov dword [edi], 2
    jmp _SwordmanTestProbeCastDone

_SwordmanTestCopyContinue:
    lea esp, [esp+4]
    jmp _SwordmanTestProbeCopyDone

_SwordmanTestProbeCopyDone:
    mov [edi+4], eax
    mov [edi+8], ecx
    mov [edi+0Ch], edx
    mov [edi+10h], esi
_SwordmanTestProbeDone:
    add esp, 20h
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
