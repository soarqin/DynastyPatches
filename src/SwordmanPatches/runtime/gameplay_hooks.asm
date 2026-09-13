BITS 32
%include "gameplay_hook_addresses.inc"

extern _g_swordman_player_turn_original
extern _g_swordman_loaded_stage

global _SwordmanSpeedDelayHook
global _SwordmanSpeedFrameValueHook
global _SwordmanSpeedActionMultiplierHook
global _SwordmanSpeedActionPositiveHook
global _SwordmanSpeedActionNegativeHook
global _SwordmanSpeedActionDamageHook
global _SwordmanSpeedActionDamageNegativeHook
global _SwordmanSpeedTimerHook
global _SwordmanChestHook

global _SwordmanAttributeCapHook0
global _SwordmanAttributeCapHook1
global _SwordmanAttributeCapHook2
global _SwordmanAttributeCapHook3
global _SwordmanAttributeCapHook4

global _SwordmanGrowthMaximumHook0
global _SwordmanGrowthMaximumHook1
global _SwordmanGrowthMaximumHook2
global _SwordmanGrowthMaximumHook3
global _SwordmanGrowthMaximumHook4
global _SwordmanGrowthMaximumHook5
global _SwordmanGrowthMaximumHook6
global _SwordmanGrowthMinimumHook0
global _SwordmanGrowthMinimumHook1
global _SwordmanGrowthMinimumHook2
global _SwordmanGrowthMinimumHook3
global _SwordmanGrowthMinimumHook4
global _SwordmanGrowthMinimumHook5
global _SwordmanGrowthMinimumHook6
global _SwordmanCastAfterMoveHook
global _SwordmanSummonItemCopyHook
global _SwordmanSaveAllowedHook
global _SwordmanLoadReadyHook
global _SwordmanPlayerTurnHook

section .text

_SwordmanSpeedDelayHook:
    mov dword [esi+28h], 0
    push SWORDMAN_SPEED_DELAY_CONTINUE
    ret

_SwordmanSpeedFrameValueHook:
    xor edx, edx
    mov [esi+34h], edx
    push SWORDMAN_SPEED_FRAME_VALUE_CONTINUE
    ret

_SwordmanSpeedActionMultiplierHook:
    mov dword [esi+20h], 3
    push SWORDMAN_SPEED_ACTION_MULTIPLIER_CONTINUE
    ret

_SwordmanSpeedActionPositiveHook:
    mov dword [esi+30h], 3
    push SWORDMAN_SPEED_ACTION_POSITIVE_CONTINUE
    ret

_SwordmanSpeedActionNegativeHook:
    mov dword [esi+30h], -3
    push SWORDMAN_SPEED_ACTION_NEGATIVE_CONTINUE
    ret

_SwordmanSpeedActionDamageHook:
    mov dword [esi+34h], 3
    push SWORDMAN_SPEED_DAMAGE_CONTINUE
    ret

_SwordmanSpeedActionDamageNegativeHook:
    mov dword [esi+34h], -3
    push SWORDMAN_SPEED_DAMAGE_NEGATIVE_CONTINUE
    ret

_SwordmanSpeedTimerHook:
    mov eax, 21h
    push SWORDMAN_SPEED_TIMER_CONTINUE
    ret

_SwordmanChestHook:
    mov word [eax+0Ch], 0
    push SWORDMAN_CHEST_CONTINUE
    ret

%macro ATTRIBUTE_CAP_HOOK 3
_SwordmanAttributeCapHook%1:
    cmp edx, 3E7h
    jle %%continue
    mov edx, 3E7h
    sub edx, ecx
    mov [esi+%3], edx
%%continue:
    push %2
    ret
%endmacro

ATTRIBUTE_CAP_HOOK 0, SWORDMAN_ATTRIBUTE_0_CONTINUE, 34h
ATTRIBUTE_CAP_HOOK 1, SWORDMAN_ATTRIBUTE_1_CONTINUE, 38h
ATTRIBUTE_CAP_HOOK 2, SWORDMAN_ATTRIBUTE_2_CONTINUE, 3Ch
ATTRIBUTE_CAP_HOOK 3, SWORDMAN_ATTRIBUTE_3_CONTINUE, 40h
ATTRIBUTE_CAP_HOOK 4, SWORDMAN_ATTRIBUTE_4_CONTINUE, 44h

%macro GROWTH_COMMON_HOOKS 4
_SwordmanGrowthMaximumHook%1:
    mov edx, ecx
    mov %3, [esi+%4]
    push %2
    ret

_SwordmanGrowthMinimumHook%1:
    cdq
    inc ecx
    mov %3, [esi+%4]
    push %2
    ret
%endmacro

GROWTH_COMMON_HOOKS 0, SWORDMAN_GROWTH_0_CONTINUE, ebx, 34h
GROWTH_COMMON_HOOKS 1, SWORDMAN_GROWTH_1_CONTINUE, ebx, 38h
GROWTH_COMMON_HOOKS 2, SWORDMAN_GROWTH_2_CONTINUE, ebx, 3Ch
GROWTH_COMMON_HOOKS 3, SWORDMAN_GROWTH_3_CONTINUE, ebx, 40h
GROWTH_COMMON_HOOKS 4, SWORDMAN_GROWTH_4_CONTINUE, ebp, 34h

_SwordmanGrowthMaximumHook5:
    mov edx, ecx
    movsx eax, bp
    push SWORDMAN_GROWTH_5_CONTINUE
    ret

_SwordmanGrowthMinimumHook5:
    cdq
    inc ecx
    movsx eax, bp
    push SWORDMAN_GROWTH_5_CONTINUE
    ret

_SwordmanGrowthMaximumHook6:
    mov edx, ecx
    movsx eax, bp
    push SWORDMAN_GROWTH_6_CONTINUE
    ret

_SwordmanGrowthMinimumHook6:
    cdq
    inc ecx
    movsx eax, bp
    push SWORDMAN_GROWTH_6_CONTINUE
    ret

_SwordmanCastAfterMoveHook:
    jnz .allow_after_move
    cmp word [eax+18h], 5
    push SWORDMAN_CAST_AFTER_MOVE_CONTINUE
    ret
.allow_after_move:
    push SWORDMAN_CAST_AFTER_MOVE_ALLOW
    ret

_SwordmanSummonItemCopyHook:
    push SWORDMAN_SUMMON_ITEM_COPY_CONTINUE
    ret

_SwordmanSaveAllowedHook:
    mov eax, 1
    ret

_SwordmanLoadReadyHook:
    ; Only the successful GAMEDATA.GCF load path reaches this site.
    mov [_g_swordman_loaded_stage], esi
    mov dword [esi+0Ch], 9
    push SWORDMAN_LOAD_READY_CONTINUE
    ret

_SwordmanPlayerTurnHook:
    ; EBP is CStageSystem; its original stack frame is already established.
    pushfd
    cmp [_g_swordman_loaded_stage], ebp
    jne .normal_turn
    mov dword [_g_swordman_loaded_stage], 0
    popfd
    ; Keep saved action/movement fields and continue input initialization.
    push SWORDMAN_PLAYER_TURN_RESUME
    ret
.normal_turn:
    popfd
    jmp [_g_swordman_player_turn_original]
