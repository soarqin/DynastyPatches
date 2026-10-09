#ifndef CASTLE_WIDESCREEN_BACKDROP_H
#define CASTLE_WIDESCREEN_BACKDROP_H

#include <stdbool.h>
#include <stdint.h>

void CaptureMapBackdrop(uintptr_t renderer, uintptr_t scene);
void InvalidateMapBackdrop(uintptr_t scene);
void ResetMapBackdrop(void);
void SetBackdropTransitionColor(int color);
bool CaptureTransitionBackdrop(uintptr_t renderer, uintptr_t scene);
bool HasTransitionBackdrop(uintptr_t renderer, uintptr_t scene);
bool ApplyMapTransition(uintptr_t renderer, int color);
bool DrawTransitionBackdrop(uintptr_t renderer, int color);
/* Write into the surface already locked by the game during frame upload. */
bool ComposeFixedFrame(uintptr_t renderer, void *destination, int32_t destination_pitch);

#endif
