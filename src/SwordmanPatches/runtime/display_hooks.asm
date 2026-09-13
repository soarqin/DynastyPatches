BITS 32

; The continuation values are read-only C objects initialized from
; display_addresses.h. This keeps all NASM output in .text.
extern _SwordmanCleanupVideo
extern _SwordmanScaleMovieBuffer
extern _g_display_original_cleanup_call
extern _SwordmanBinkBlitRectContinueAddress
extern _SwordmanBinkFrameLayoutContinueAddress
extern _SwordmanMovieBufferLayoutContinueAddress
extern _SwordmanMoviePaintContinueAddress
extern _SwordmanMoviePaintOriginalCallAddress

global _SwordmanMovieCleanupHook
global _SwordmanBinkBlitRectHook
global _SwordmanBinkFrameLayoutHook
global _SwordmanMovieBufferLayoutHook
global _SwordmanMoviePaintHook

section .text

_SwordmanMovieCleanupHook:
    pushfd
    pushad
    push esi
    call _SwordmanCleanupVideo
    add esp, 4
    popad
    popfd
    ; MinHook relocates the original call and continues after it.
    jmp [_g_display_original_cleanup_call]

_SwordmanBinkBlitRectHook:
    ; Original bytes at 0x004038EA are add edx, 3Ch; push edx; push eax.
    add edx, 34h
    push edx
    push eax
    jmp [_SwordmanBinkBlitRectContinueAddress]

_SwordmanBinkFrameLayoutHook:
    ; Replace mov ecx, [eax+14h]; mov edx, [eax+10h].
    mov ecx, [eax+08h]
    mov edx, [eax+0Ch]
    jmp [_SwordmanBinkFrameLayoutContinueAddress]

_SwordmanMovieBufferLayoutHook:
    ; XP Bink uses the original buffer layout. Scale the real buffer after
    ; BinkBufferSetOffset, then reproduce the overwritten game instructions.
    pushfd
    pushad
    push esi
    call _SwordmanScaleMovieBuffer
    add esp, 4
    popad
    popfd
    mov edx, [esi+18h]
    push 1
    mov [0058F174h], edx
    jmp [_SwordmanMovieBufferLayoutContinueAddress]

_SwordmanMoviePaintHook:
    ; Reproduce mod exe 0x0045049B: draw the game first, then blit Bink
    ; before continuing into the default window procedure path.
    mov ecx, [0058F16Ch]
    call [_SwordmanMoviePaintOriginalCallAddress]
    mov edx, [0058F178h]
    test edx, edx
    jz .done
.blit:
    push 1
    push 0
    push edx
    call [0045B1B4h]
.done:
    jmp [_SwordmanMoviePaintContinueAddress]
