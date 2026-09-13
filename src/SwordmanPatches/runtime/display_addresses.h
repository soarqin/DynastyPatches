#ifndef SWORDMAN_DISPLAY_ADDRESSES_H
#define SWORDMAN_DISPLAY_ADDRESSES_H

#include <stdint.h>

/* Addresses belong to the supported Sm_win2.exe image. */
#define SWORDMAN_MOVIE_CLEANUP_HOOK_ADDRESS ((uintptr_t)0x00403835u)
#define SWORDMAN_MOVIE_OBJECT_BINK_OFFSET 0x18u
#define SWORDMAN_MOVIE_OBJECT_BUFFER_OFFSET 0x1Cu
#define SWORDMAN_CURRENT_BINK_ADDRESS ((uintptr_t)0x0058F174u)
#define SWORDMAN_CURRENT_BUFFER_ADDRESS ((uintptr_t)0x0058F178u)

#define SWORDMAN_BINK_BLIT_RECT_HOOK_ADDRESS ((uintptr_t)0x004038EAu)
#define SWORDMAN_BINK_BLIT_RECT_CONTINUE_ADDRESS ((uintptr_t)0x004038EFu)
#define SWORDMAN_BINK_FRAME_LAYOUT_HOOK_ADDRESS ((uintptr_t)0x004038F8u)
#define SWORDMAN_BINK_FRAME_LAYOUT_CONTINUE_ADDRESS ((uintptr_t)0x004038FEu)
#define SWORDMAN_WINDOW_PROC_HOOK_ADDRESS ((uintptr_t)0x00450450u)

#define SWORDMAN_NO_CD_HOOK_ADDRESS ((uintptr_t)0x004129D0u)
#define SWORDMAN_ARCHIVE_CD_DIRECTORY_OFFSET 0x25Cu
#define SWORDMAN_ARCHIVE_PATH_CAPACITY 0x100u
#define SWORDMAN_CREATE_WINDOW_CALL_ADDRESS ((uintptr_t)0x0045033Au)
#define SWORDMAN_CREATE_WINDOW_RETURN_ADDRESS ((uintptr_t)0x00450340u)
#define SWORDMAN_CREATE_WINDOW_STYLE_ADDRESS ((uintptr_t)0x00450332u)
#define SWORDMAN_STRETCH_DIBITS_CALL_ADDRESS ((uintptr_t)0x00405D4Cu)
#define SWORDMAN_STRETCH_DIBITS_RETURN_ADDRESS ((uintptr_t)0x00405D52u)
#define SWORDMAN_BINK_BUFFER_OPEN_CALL_ADDRESS ((uintptr_t)0x00403A42u)
#define SWORDMAN_BINK_BUFFER_OPEN_RETURN_ADDRESS ((uintptr_t)0x00403A4Du)
#define SWORDMAN_BINK_BUFFER_SET_OFFSET_CALL_ADDRESS ((uintptr_t)0x00403A80u)
#define SWORDMAN_BINK_BUFFER_SET_OFFSET_RETURN_ADDRESS ((uintptr_t)0x00403A8Bu)
#define SWORDMAN_MOVIE_BUFFER_LAYOUT_HOOK_ADDRESS ((uintptr_t)0x00403A8Bu)
#define SWORDMAN_MOVIE_BUFFER_LAYOUT_CONTINUE_ADDRESS ((uintptr_t)0x00403A96u)
#define SWORDMAN_MOVIE_PAINT_HOOK_ADDRESS ((uintptr_t)0x0045049Bu)
#define SWORDMAN_MOVIE_PAINT_CONTINUE_ADDRESS ((uintptr_t)0x004504A6u)
#define SWORDMAN_MOVIE_PAINT_ORIGINAL_CALL_ADDRESS ((uintptr_t)0x00403380u)

/* These are initialized from the macros so the NASM text has no data section. */
extern const uintptr_t SwordmanBinkBlitRectContinueAddress;
extern const uintptr_t SwordmanBinkFrameLayoutContinueAddress;
extern const uintptr_t SwordmanMovieBufferLayoutContinueAddress;
extern const uintptr_t SwordmanMoviePaintContinueAddress;
extern const uintptr_t SwordmanMoviePaintOriginalCallAddress;

#endif
