#include "runtime_internal.h"
#include "../swordman_addresses.h"

#include <dbghelp.h>
#include <MinHook.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(void *) == 4, "SwordmanRuntime must be x86");

static size_t g_image_size;
static LONG g_started;
static LPTOP_LEVEL_EXCEPTION_FILTER g_previous_filter;

static LONG WINAPI CrashDump(EXCEPTION_POINTERS *exception) {
    wchar_t path[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (length != 0 && length < MAX_PATH) {
        wchar_t *slash = wcsrchr(path, L'\\');
        if (slash != NULL) {
            _snwprintf_s(slash + 1, MAX_PATH - (size_t)(slash + 1 - path), _TRUNCATE,
                         L"SwordmanRuntime-crash-%lu.dmp", GetCurrentProcessId());
            HANDLE dump = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, NULL);
            if (dump != INVALID_HANDLE_VALUE) {
                MINIDUMP_EXCEPTION_INFORMATION info = {GetCurrentThreadId(), exception, FALSE};
                MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                                  MiniDumpWithFullMemory | MiniDumpWithUnloadedModules,
                                  &info, NULL, NULL);
                CloseHandle(dump);
            }
        }
    }
    return g_previous_filter != NULL ? g_previous_filter(exception) : EXCEPTION_CONTINUE_SEARCH;
}

bool ValidateGameBytes(uintptr_t address, const uint8_t *expected, size_t length) {
    MEMORY_BASIC_INFORMATION region;
    if (address < SWORDMAN_IMAGE_BASE || length > g_image_size ||
        address - SWORDMAN_IMAGE_BASE > g_image_size - length ||
        VirtualQuery((LPCVOID)address, &region, sizeof(region)) != sizeof(region) ||
        region.State != MEM_COMMIT || (region.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0 ||
        length > region.RegionSize - (address - (uintptr_t)region.BaseAddress) ||
        memcmp((LPCVOID)address, expected, length) != 0) {
        SetLastError(ERROR_REVISION_MISMATCH);
        return false;
    }
    return true;
}

bool CreateGameHook(uintptr_t address, const uint8_t *expected, size_t length,
                    LPVOID detour, LPVOID *original) {
    if (!ValidateGameBytes(address, expected, length)) return false;
    if (MH_CreateHook((LPVOID)address, detour, original) == MH_OK) return true;
    SetLastError(ERROR_FUNCTION_FAILED);
    return false;
}

DWORD WINAPI SwordmanRuntimeStart(const SwordmanOptions *options) {
    if (options == NULL || (options->flags & ~(uint32_t)SWORDMAN_ALL_FLAGS) != 0 ||
        options->growth > SWORDMAN_GROWTH_MINIMUM ||
        options->scale2 < 2 || options->scale2 > 6 ||
        ((options->flags & SWORDMAN_SOFTWARE_MOVIE) && !(options->flags & SWORDMAN_MOVIE))) {
        return ERROR_INVALID_PARAMETER;
    }
    if (InterlockedCompareExchange(&g_started, 1, 0) != 0) return ERROR_ALREADY_INITIALIZED;
    const uint8_t *base = (const uint8_t *)GetModuleHandleW(NULL);
    if ((uintptr_t)base != SWORDMAN_IMAGE_BASE) return ERROR_REVISION_MISMATCH;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096) {
        return ERROR_BAD_EXE_FORMAT;
    }
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->FileHeader.TimeDateStamp != SWORDMAN_TIMESTAMP ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.AddressOfEntryPoint != SWORDMAN_ENTRY_RVA) return ERROR_REVISION_MISMATCH;
    wchar_t path[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return ERROR_BAD_PATHNAME;
    if (!SwordmanHashFile(path, kSwordmanImageHash)) return GetLastError();
    g_image_size = nt->OptionalHeader.SizeOfImage;
    if (MH_Initialize() != MH_OK) return ERROR_FUNCTION_FAILED;
    if (!CreateGameplayHooks(options) || !CreateDisplayHooks(options)) {
        DWORD error = GetLastError();
        MH_Uninitialize();
        return error != ERROR_SUCCESS ? error : ERROR_FUNCTION_FAILED;
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        MH_Uninitialize();
        return ERROR_FUNCTION_FAILED;
    }
    g_previous_filter = SetUnhandledExceptionFilter(CrashDump);
    return ERROR_SUCCESS;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
