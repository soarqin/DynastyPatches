BITS 32
%define SWORDMAN_LOAD_READY_CONTINUE _SwordmanTestLoadReadyContinue
%define SWORDMAN_PLAYER_TURN_RESUME _SwordmanTestPlayerTurnResume
%include "runtime/gameplay_hooks.asm"

extern _g_original_calls

global _SwordmanTestSaveAllowed
global _SwordmanTestLoadReady
global _SwordmanTestPlayerTurn
global _SwordmanTestLoadReadyContinue
global _SwordmanTestPlayerTurnResume
global _SwordmanTestPlayerTurnOriginal
global _SwordmanTestPlayerTurnOriginalAddress
section .text

_SwordmanTestSaveAllowed:
    call _SwordmanSaveAllowedHook
    ret

_SwordmanTestLoadReady:
    push esi
    mov esi, [esp+8]
    call _SwordmanLoadReadyHook
    pop esi
    ret

_SwordmanTestPlayerTurn:
    push ebp
    mov ebp, [esp+8]
    call _SwordmanPlayerTurnHook
    pop ebp
    ret

_SwordmanTestLoadReadyContinue:
    ret

_SwordmanTestPlayerTurnResume:
    ret

_SwordmanTestPlayerTurnOriginal:
    inc dword [_g_original_calls]
    ret

_SwordmanTestPlayerTurnOriginalAddress:
    mov eax, _SwordmanTestPlayerTurnOriginal
    ret
