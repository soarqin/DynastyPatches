BITS 32

%include "runtime_hook_addresses.inc"

extern _ApplySurfaceFormat
extern _ApplyWidescreenRendererWidth
extern _MarkMapFrame
extern _MarkBinkFrame
extern _OffsetMapUiSpriteX
extern _ApplyEncounterInitialRate
extern _ApplyEncounterThresholdRate

extern _g_original_surface_format
extern _g_original_renderer_width
extern _g_original_map_render
extern _g_original_map_ui_sprite
extern _g_original_dialog_name
extern _g_original_dialog_body
extern _g_original_glyph_blit
extern _g_original_bink_frame
extern _g_original_fullscreen_vblank
extern _g_original_encounter_initial
extern _g_experience_multiplier
extern _g_money_multiplier
extern _g_runtime_shutting_down
extern _PrepareFrameTimer
extern _ShouldRunGameLogic
extern _AdjustCursorPosition

global _SurfaceFormatHook
global _RendererWidthHook
global _MapRenderHook
global _MapUiSpriteHook
global _DialogTextNameHook
global _DialogTextBodyHook
global _GlyphBlitHook
global _BinkFrameHook
global _BinkPresentCallHook
global _FullscreenVblankHook
global _ExperienceGainHook
global _ExperienceDisplayHook
global _MoneyGainHook
global _MoneyDisplayHook
global _EncounterInitialHook
global _EncounterRegenerationHook
extern _g_original_timer_setup
extern _g_original_game_logic_tick
extern _g_original_cursor_position

section .text

_SurfaceFormatHook:
    pushad
    mov eax, [esp+18h]
    push eax
    call _ApplySurfaceFormat
    add esp, 4
    popad
    jmp [_g_original_surface_format]

_RendererWidthHook:
    pushad
    call _ApplyWidescreenRendererWidth
    popad
    jmp [_g_original_renderer_width]

_MapRenderHook:
    pushad
    call _MarkMapFrame
    popad
    jmp [_g_original_map_render]

_MapUiSpriteHook:
    pushad
    mov eax, [esp+18h]
    mov edx, [esp+0Ch]
    push dword [edx]
    push eax
    call _OffsetMapUiSpriteX
    add esp, 8
    popad
    jmp [_g_original_map_ui_sprite]

_DialogTextNameHook:
    add dword [esp+0Ch], 70h
    jmp [_g_original_dialog_name]

_DialogTextBodyHook:
    add dword [esp+0Ch], 70h
    jmp [_g_original_dialog_body]

_GlyphBlitHook:
    mov eax, [esp+10h]
    cmp eax, [esp+1Ch]
    je .glyph_invalid
    mov eax, [esp+14h]
    cmp eax, [esp+20h]
    je .glyph_invalid
    jmp [_g_original_glyph_blit]
.glyph_invalid:
    xor eax, eax
    ret

_BinkFrameHook:
    pushad
    call _MarkBinkFrame
    popad
    jmp [_g_original_bink_frame]

_BinkPresentCallHook:
    ; MH_CreateHook replaces this call site with JMP, so [esp] is the
    ; original argument and there is no synthetic return address.
    push esi
    mov esi, ecx
    push dword [esp+4]
    mov eax, CASTLE_BINK_UNLOCK_FUNCTION_ADDRESS
    call eax
    cmp dword [_g_runtime_shutting_down], 0
    jne .bink_skip_present
    mov ecx, esi
    mov eax, CASTLE_WINDOWED_PRESENT_FUNCTION_ADDRESS
    call eax
.bink_skip_present:
    pop esi
    add esp, 4
    mov eax, CASTLE_BINK_PRESENT_CONTINUE_ADDRESS
    jmp eax

_FullscreenVblankHook:
    pushad
    mov eax, [esp+4]
    mov eax, [eax]
    mov ecx, [eax]
    push eax
    push 0
    push 1
    call [ecx+58h]
    popad
    jmp [_g_original_fullscreen_vblank]

_ExperienceGainHook:
    push ecx
    mov ecx, ebx
    imul ecx, [_g_experience_multiplier]
    add eax, ecx
    pop ecx
    mov [edi+24h], eax
    mov eax, CASTLE_EXPERIENCE_GAIN_CONTINUE_ADDRESS
    jmp eax

_ExperienceDisplayHook:
    mov ecx, [CASTLE_EXPERIENCE_DISPLAY_COLOR_ADDRESS]
    mov eax, ebx
    imul eax, [_g_experience_multiplier]
    push eax
    mov eax, CASTLE_EXPERIENCE_DISPLAY_CONTINUE_ADDRESS
    jmp eax

_MoneyGainHook:
    mov edx, [ecx+5DD8h]
    push eax
    imul eax, [_g_money_multiplier]
    add edx, eax
    pop eax
    mov eax, CASTLE_MONEY_GAIN_CONTINUE_ADDRESS
    jmp eax

_MoneyDisplayHook:
    mov ecx, [eax]
    mov edx, [eax+8]
    push ecx
    mov eax, ebp
    imul eax, [_g_money_multiplier]
    push eax
    push edx
    lea eax, [esp+1BCh]
    mov edx, CASTLE_MONEY_DISPLAY_CONTINUE_ADDRESS
    jmp edx

_EncounterInitialHook:
    pushad
    push ebx
    call _ApplyEncounterInitialRate
    add esp, 4
    popad
    jmp [_g_original_encounter_initial]

_EncounterRegenerationHook:
    pushad
    call _ApplyEncounterThresholdRate
    popad
    cmp byte [CASTLE_ENCOUNTER_MAP_SPECIAL_FLAG_ADDRESS], 0
    jne .encounter_regeneration_return
    push CASTLE_ENCOUNTER_REGENERATION_SKIP_ADDRESS
    ret
.encounter_regeneration_return:
    push CASTLE_ENCOUNTER_REGENERATION_RETURN_ADDRESS
    ret

_FramePacingTimerSetupHook:
    pushad
    call _PrepareFrameTimer
    popad
    jmp [_g_original_timer_setup]

_GameLogicTickHook:
    pushad
    call _ShouldRunGameLogic
    test eax, eax
    jz .game_logic_tick_skip
    popad
    jmp [_g_original_game_logic_tick]
.game_logic_tick_skip:
    popad
    ret

_CursorPositionHook:
    pushad
    mov eax, [esp+0x18]
    lea edx, [esp+0x10]
    push edx
    push eax
    call _AdjustCursorPosition
    add esp, 8
    popad
    jmp [_g_original_cursor_position]
