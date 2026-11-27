/* Validate MinHook entry points by mapping RPG.exe without executing game code. */
#include <windows.h>
#include <MinHook.h>
#include "../src/CastlePatches/castle_addresses.h"

#include <stdio.h>

static void DummyHook(void) {}

int wmain(void) {
    HANDLE file = CreateFileW(L"E:/Games/Dynasty/Castle/exe/RPG.exe", GENERIC_READ,
                              FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 1;
    HANDLE section = CreateFileMappingW(file, NULL, PAGE_READONLY | SEC_IMAGE, 0, 0, NULL);
    CloseHandle(file);
    if (section == NULL) return 2;
    /* Map the image only: no entry point, imports or game code is executed. */
    const uint8_t *image = MapViewOfFile(section, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(section);
    if (image == NULL) return 3;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(image + dos->e_lfanew);
    uintptr_t preferred_base = nt->OptionalHeader.ImageBase;
    uintptr_t addresses[] = {
        MAP_EFFECT_FUNCTION_ADDRESS, FADE_OUT_FUNCTION_ADDRESS, FADE_IN_FUNCTION_ADDRESS,
        SCRIPT_COMMAND_FACTORY_ADDRESS, MAP_SCENE_LOAD_FUNCTION_ADDRESS,
        MAP_SCENE_RESUME_FUNCTION_ADDRESS, MAP_SCENE_SUSPEND_FUNCTION_ADDRESS,
        ORDINARY_MENU_RELEASE_FUNCTION_ADDRESS, SYSTEM_MENU_RELEASE_FUNCTION_ADDRESS, UI_OBJECT_DRAW_FUNCTION_ADDRESS,
        UI_SPRITE_BLIT_FUNCTION_ADDRESS, SAVE_LOAD_FUNCTION_ADDRESS,
        ENCOUNTER_BATTLE_CAPTURE_CALL_ADDRESS, SCRIPT_BATTLE_CAPTURE_CALL_ADDRESS,
        WINDOWED_UPLOAD_CLEAR_ADDRESS,
    };
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) return 4;
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        LPVOID trampoline = NULL;
        LPVOID target = (LPVOID)(image + addresses[i] - preferred_base);
        LPVOID detour = addresses[i] == WINDOWED_UPLOAD_CLEAR_ADDRESS
            ? (LPVOID)(image + WINDOWED_UPLOAD_LOCK_ADDRESS - preferred_base)
            : (LPVOID)(uintptr_t)DummyHook;
        status = MH_CreateHook(target, detour, &trampoline);
        printf("%08lX create=%s trampoline=%s\n", (unsigned long)addresses[i],
               MH_StatusToString(status), trampoline != NULL ? "yes" : "no");
        if (status != MH_OK) return 5;
    }
    status = MH_EnableHook(MH_ALL_HOOKS);
    printf("enable=%s\n", MH_StatusToString(status));
    if (status != MH_OK) return 6;
    status = MH_DisableHook(MH_ALL_HOOKS);
    printf("disable=%s\n", MH_StatusToString(status));
    if (status != MH_OK) return 7;
    status = MH_Uninitialize();
    printf("uninitialize=%s\n", MH_StatusToString(status));
    if (status != MH_OK) return 8;
    UnmapViewOfFile(image);
    puts("No game entry point or game function was executed.");
    return 0;
}
