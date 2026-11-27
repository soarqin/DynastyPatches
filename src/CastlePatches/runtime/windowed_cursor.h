#ifndef CASTLE_WINDOWED_CURSOR_H
#define CASTLE_WINDOWED_CURSOR_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

bool BeginWindowedCursorFrame(uintptr_t renderer, LONG x, LONG y);
uint16_t *WindowedCursorPixels(bool white);
void EndWindowedCursorFrame(void);
void PresentWindowedCursor(HWND window, uint32_t scale4);
HCURSOR GetWindowedCursor(void);
void ReleaseWindowedCursor(void);

#endif
