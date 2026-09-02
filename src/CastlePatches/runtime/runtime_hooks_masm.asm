.386
.model flat, C
option casemap:none

INCLUDE runtime_hook_addresses.inc

EXTERN ApplySurfaceFormat:PROC
EXTERN ApplyWidescreenRendererWidth:PROC
EXTERN MarkMapFrame:PROC
EXTERN MarkBinkFrame:PROC
EXTERN OffsetMapUiSpriteX:PROC
EXTERN ApplyEncounterInitialRate:PROC
EXTERN ApplyEncounterThresholdRate:PROC
EXTERN PrepareFrameTimer:PROC
EXTERN ShouldRunGameLogic:PROC
EXTERN AdjustCursorPosition:PROC

EXTERN g_original_surface_format:DWORD
EXTERN g_original_renderer_width:DWORD
EXTERN g_original_map_render:DWORD
EXTERN g_original_map_ui_sprite:DWORD
EXTERN g_original_dialog_name:DWORD
EXTERN g_original_dialog_body:DWORD
EXTERN g_original_glyph_blit:DWORD
EXTERN g_original_bink_frame:DWORD
EXTERN g_original_fullscreen_vblank:DWORD
EXTERN g_original_encounter_initial:DWORD
EXTERN g_experience_multiplier:DWORD
EXTERN g_money_multiplier:DWORD
EXTERN g_runtime_shutting_down:DWORD
EXTERN g_original_timer_setup:DWORD
EXTERN g_original_game_logic_tick:DWORD
EXTERN g_original_cursor_position:DWORD

PUBLIC SurfaceFormatHook
PUBLIC RendererWidthHook
PUBLIC MapRenderHook
PUBLIC MapUiSpriteHook
PUBLIC DialogTextNameHook
PUBLIC DialogTextBodyHook
PUBLIC GlyphBlitHook
PUBLIC BinkFrameHook
PUBLIC BinkPresentCallHook
PUBLIC FullscreenVblankHook
PUBLIC ExperienceGainHook
PUBLIC ExperienceDisplayHook
PUBLIC MoneyGainHook
PUBLIC MoneyDisplayHook
PUBLIC EncounterInitialHook
PUBLIC EncounterRegenerationHook
PUBLIC FramePacingTimerSetupHook
PUBLIC GameLogicTickHook
PUBLIC CursorPositionHook

.code

SurfaceFormatHook PROC
    pushad
    mov eax, DWORD PTR [esp+18h]
    push eax
    call ApplySurfaceFormat
    add esp, 4
    popad
    jmp DWORD PTR [g_original_surface_format]
SurfaceFormatHook ENDP

RendererWidthHook PROC
    pushad
    call ApplyWidescreenRendererWidth
    popad
    jmp DWORD PTR [g_original_renderer_width]
RendererWidthHook ENDP

MapRenderHook PROC
    pushad
    call MarkMapFrame
    popad
    jmp DWORD PTR [g_original_map_render]
MapRenderHook ENDP

MapUiSpriteHook PROC
    pushad
    mov eax, DWORD PTR [esp+18h]
    mov edx, DWORD PTR [esp+0Ch]
    push DWORD PTR [edx]
    push eax
    call OffsetMapUiSpriteX
    add esp, 8
    popad
    jmp DWORD PTR [g_original_map_ui_sprite]
MapUiSpriteHook ENDP

DialogTextNameHook PROC
    add DWORD PTR [esp+0Ch], 70h
    jmp DWORD PTR [g_original_dialog_name]
DialogTextNameHook ENDP

DialogTextBodyHook PROC
    add DWORD PTR [esp+0Ch], 70h
    jmp DWORD PTR [g_original_dialog_body]
DialogTextBodyHook ENDP

GlyphBlitHook PROC
    mov eax, DWORD PTR [esp+10h]
    cmp eax, DWORD PTR [esp+1Ch]
    je short glyph_invalid
    mov eax, DWORD PTR [esp+14h]
    cmp eax, DWORD PTR [esp+20h]
    je short glyph_invalid
    jmp DWORD PTR [g_original_glyph_blit]
glyph_invalid:
    xor eax, eax
    ret
GlyphBlitHook ENDP

BinkFrameHook PROC
    pushad
    call MarkBinkFrame
    popad
    jmp DWORD PTR [g_original_bink_frame]
BinkFrameHook ENDP

BinkPresentCallHook PROC
    ; MH_CreateHook replaces this call site with JMP, so [esp] is the
    ; original argument and there is no synthetic return address.
    push esi
    mov esi, ecx
    push DWORD PTR [esp+4]
    mov eax, CASTLE_BINK_UNLOCK_FUNCTION_ADDRESS
    call eax
    cmp DWORD PTR [g_runtime_shutting_down], 0
    jne bink_skip_present
    mov ecx, esi
    mov eax, CASTLE_WINDOWED_PRESENT_FUNCTION_ADDRESS
    call eax
bink_skip_present:
    pop esi
    add esp, 4
    mov eax, CASTLE_BINK_PRESENT_CONTINUE_ADDRESS
    jmp eax
BinkPresentCallHook ENDP

FullscreenVblankHook PROC
    pushad
    mov eax, DWORD PTR [esp+4]
    mov eax, DWORD PTR [eax]
    mov ecx, DWORD PTR [eax]
    push eax
    push 0
    push 1
    call DWORD PTR [ecx+58h]
    popad
    jmp DWORD PTR [g_original_fullscreen_vblank]
FullscreenVblankHook ENDP

ExperienceGainHook PROC
    push ecx
    mov ecx, ebx
    imul ecx, DWORD PTR [g_experience_multiplier]
    add eax, ecx
    pop ecx
    mov DWORD PTR [edi+24h], eax
    mov eax, CASTLE_EXPERIENCE_GAIN_CONTINUE_ADDRESS
    jmp eax
ExperienceGainHook ENDP

ExperienceDisplayHook PROC
    mov ecx, DWORD PTR [CASTLE_EXPERIENCE_DISPLAY_COLOR_ADDRESS]
    mov eax, ebx
    imul eax, DWORD PTR [g_experience_multiplier]
    push eax
    mov eax, CASTLE_EXPERIENCE_DISPLAY_CONTINUE_ADDRESS
    jmp eax
ExperienceDisplayHook ENDP

MoneyGainHook PROC
    mov edx, DWORD PTR [ecx+5DD8h]
    push eax
    imul eax, DWORD PTR [g_money_multiplier]
    add edx, eax
    pop eax
    mov eax, CASTLE_MONEY_GAIN_CONTINUE_ADDRESS
    jmp eax
MoneyGainHook ENDP

MoneyDisplayHook PROC
    mov ecx, DWORD PTR [eax]
    mov edx, DWORD PTR [eax+8]
    push ecx
    mov eax, ebp
    imul eax, DWORD PTR [g_money_multiplier]
    push eax
    push edx
    lea eax, [esp+1BCh]
    mov edx, CASTLE_MONEY_DISPLAY_CONTINUE_ADDRESS
    jmp edx
MoneyDisplayHook ENDP

EncounterInitialHook PROC
    pushad
    push ebx
    call ApplyEncounterInitialRate
    add esp, 4
    popad
    jmp DWORD PTR [g_original_encounter_initial]
EncounterInitialHook ENDP

EncounterRegenerationHook PROC
    pushad
    call ApplyEncounterThresholdRate
    popad
    cmp BYTE PTR [CASTLE_ENCOUNTER_MAP_SPECIAL_FLAG_ADDRESS], 0
    jne short encounter_regeneration_return
    push CASTLE_ENCOUNTER_REGENERATION_SKIP_ADDRESS
    ret
encounter_regeneration_return:
    push CASTLE_ENCOUNTER_REGENERATION_RETURN_ADDRESS
    ret
EncounterRegenerationHook ENDP

FramePacingTimerSetupHook PROC
    pushad
    call PrepareFrameTimer
    popad
    jmp DWORD PTR [g_original_timer_setup]
FramePacingTimerSetupHook ENDP

GameLogicTickHook PROC
    pushad
    call ShouldRunGameLogic
    test eax, eax
    jz game_logic_tick_skip
    popad
    jmp DWORD PTR [g_original_game_logic_tick]
game_logic_tick_skip:
    popad
    ret
GameLogicTickHook ENDP

CursorPositionHook PROC
    pushad
    mov eax, DWORD PTR [esp+18h]
    lea edx, DWORD PTR [esp+10h]
    push edx
    push eax
    call AdjustCursorPosition
    add esp, 8
    popad
    jmp DWORD PTR [g_original_cursor_position]
CursorPositionHook ENDP

END
