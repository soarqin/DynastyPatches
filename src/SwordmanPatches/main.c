#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include "file_identity.h"
#include "swordman.h"
#include "swordman_addresses.h"
#include "runtime_loader_stub_blob.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

enum {
    ID_NO_CD = 100,
    ID_SPEED,
    ID_CHESTS,
    ID_ATTRIBUTES,
    ID_CAST_AFTER_MOVE,
    ID_SUMMON_ITEM_COPY,
    ID_SAVE_ANYTIME,
    ID_GROWTH_NORMAL,
    ID_GROWTH_MAX,
    ID_GROWTH_MIN,
    ID_RESIZABLE,
    ID_MOVIE,
    ID_SMOOTH,
    ID_CLEANUP,
    ID_SOFTWARE_MOVIE,
    ID_LAUNCH,
    ID_STATUS,
};

enum {
    STUB_PATH = 0x00,
    STUB_KERNEL = 0x200,
    STUB_LOAD = 0x220,
    STUB_START = 0x240,
    STUB_OPTIONS = 0x260,
    STUB_GETMODULE = 0x268,
    STUB_GETPROC = 0x26c,
    STUB_DATA_SIZE = 0x280,
};

static HINSTANCE g_instance;
static HWND g_window;
static HWND g_status;
static HFONT g_font;

static const wchar_t kConfigFileName[] = L"SwordmanPatches.ini";

static bool ExistingFile(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool LauncherDirectory(wchar_t *path, size_t capacity) {
    DWORD length = GetModuleFileNameW(NULL, path, (DWORD)capacity);
    if (length == 0 || length >= capacity) return false;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash == NULL) return false;
    slash[1] = L'\0';
    return true;
}

static bool ConfigPath(wchar_t *path, size_t capacity) {
    if (!LauncherDirectory(path, capacity)) return false;
    return wcscat_s(path, capacity, kConfigFileName) == 0;
}

static bool ResolveRelativeGamePath(wchar_t *path, size_t capacity) {
    static const wchar_t *const candidates[] = {
        L".\\Sm_win2.exe",
        L".\\exe\\Sm_win2.exe",
        L".\\Swordman\\Sm_win2.exe",
        L".\\exe\\Swordman\\Sm_win2.exe",
    };

    for (size_t index = 0; index < _countof(candidates); ++index) {
        DWORD length = GetFullPathNameW(candidates[index], (DWORD)capacity, path, NULL);
        if (length == 0 || length >= capacity) continue;
        if (ExistingFile(path)) return true;
    }
    return false;
}

static bool LoadSavedGamePath(wchar_t *path, size_t capacity) {
    wchar_t config_path[MAX_PATH];
    if (!ConfigPath(config_path, _countof(config_path))) return false;
    DWORD length = GetPrivateProfileStringW(L"Game", L"Path", L"", path,
                                            (DWORD)capacity, config_path);
    return length > 0 && length < capacity && ExistingFile(path);
}

static void SaveGamePath(const wchar_t *path) {
    wchar_t config_path[MAX_PATH];
    if (ConfigPath(config_path, _countof(config_path))) {
        WritePrivateProfileStringW(L"Game", L"Path", path, config_path);
    }
}

static bool LoadConfigBool(const wchar_t *key, bool default_value) {
    wchar_t config_path[MAX_PATH];
    if (!ConfigPath(config_path, _countof(config_path))) return default_value;
    return GetPrivateProfileIntW(L"Patches", key, default_value ? 1 : 0, config_path) != 0;
}

static uint32_t LoadConfigGrowth(void) {
    wchar_t config_path[MAX_PATH];
    if (!ConfigPath(config_path, _countof(config_path))) return SWORDMAN_GROWTH_NORMAL;
    int value = GetPrivateProfileIntW(L"Patches", L"Growth", SWORDMAN_GROWTH_NORMAL, config_path);
    return value >= SWORDMAN_GROWTH_NORMAL && value <= SWORDMAN_GROWTH_MINIMUM
               ? (uint32_t)value
               : SWORDMAN_GROWTH_NORMAL;
}

static void SaveOptions(const SwordmanOptions *options) {
    wchar_t config_path[MAX_PATH];
    if (!ConfigPath(config_path, _countof(config_path))) return;

    struct FlagSetting {
        const wchar_t *key;
        uint32_t flag;
    };
    static const struct FlagSetting settings[] = {
        {L"NoCd", SWORDMAN_NO_CD},
        {L"Speed", SWORDMAN_SPEED},
        {L"Chests", SWORDMAN_CHESTS},
        {L"Attributes", SWORDMAN_ATTRIBUTE_999},
        {L"CastAfterMove", SWORDMAN_CAST_AFTER_MOVE},
        {L"SummonItemCopy", SWORDMAN_SUMMON_ITEM_COPY},
        {L"SaveAnytime", SWORDMAN_SAVE_ANYTIME},
        {L"Resizable", SWORDMAN_RESIZABLE},
        {L"Movie", SWORDMAN_MOVIE},
        {L"Smooth", SWORDMAN_SMOOTH},
        {L"MovieCleanup", SWORDMAN_MOVIE_CLEANUP},
        {L"SoftwareMovie", SWORDMAN_SOFTWARE_MOVIE},
    };
    for (size_t index = 0; index < _countof(settings); ++index) {
        WritePrivateProfileStringW(L"Patches",
                                   settings[index].key,
                                   (options->flags & settings[index].flag) != 0 ? L"1" : L"0",
                                   config_path);
    }

    wchar_t growth[16];
    _snwprintf_s(growth, _countof(growth), _TRUNCATE, L"%lu", (unsigned long)options->growth);
    WritePrivateProfileStringW(L"Patches", L"Growth", growth, config_path);
}

static bool ReadWholeFile(const wchar_t *path, uint8_t **data, size_t *size) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length;
    bool success = GetFileSizeEx(file, &length) && length.QuadPart > 0 &&
                   (uint64_t)length.QuadPart <= SIZE_MAX;
    uint8_t *copy = NULL;
    if (success) {
        copy = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length.QuadPart);
        DWORD read = 0;
        success = copy != NULL && ReadFile(file, copy, (DWORD)length.QuadPart, &read, NULL) &&
                  read == (DWORD)length.QuadPart;
    }
    CloseHandle(file);
    if (!success) {
        HeapFree(GetProcessHeap(), 0, copy);
        return false;
    }
    *data = copy;
    *size = (size_t)length.QuadPart;
    return true;
}

static const uint8_t *RvaToFile(const uint8_t *file, size_t size,
                                const IMAGE_NT_HEADERS32 *nt, uint32_t rva) {
    const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        uint32_t start = section[index].VirtualAddress;
        uint32_t span = section[index].SizeOfRawData > section[index].Misc.VirtualSize
                            ? section[index].SizeOfRawData : section[index].Misc.VirtualSize;
        if (rva >= start && rva - start < span) {
            uint64_t offset = (uint64_t)section[index].PointerToRawData + rva - start;
            return offset < size ? file + offset : NULL;
        }
    }
    return NULL;
}

static bool FindImportSlot(const wchar_t *path, const char *module, const char *name,
                           uint32_t *slot_address) {
    uint8_t *file = NULL;
    size_t size = 0;
    bool found = false;
    if (!ReadWholeFile(path, &file, &size)) return false;
    if (size < sizeof(IMAGE_DOS_HEADER)) goto done;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)file;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        (size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS32) > size) goto done;
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(file + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.ImageBase != SWORDMAN_IMAGE_BASE) goto done;
    IMAGE_DATA_DIRECTORY directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const IMAGE_IMPORT_DESCRIPTOR *imports = (const IMAGE_IMPORT_DESCRIPTOR *)RvaToFile(file, size, nt, directory.VirtualAddress);
    if (imports == NULL) goto done;
    for (; imports->Name != 0; ++imports) {
        const char *dll = (const char *)RvaToFile(file, size, nt, imports->Name);
        if (dll == NULL || _stricmp(dll, module) != 0) continue;
        uint32_t names_rva = imports->OriginalFirstThunk != 0 ? imports->OriginalFirstThunk : imports->FirstThunk;
        const IMAGE_THUNK_DATA32 *names = (const IMAGE_THUNK_DATA32 *)RvaToFile(file, size, nt, names_rva);
        if (names == NULL) goto done;
        for (uint32_t index = 0; names[index].u1.AddressOfData != 0; ++index) {
            if (IMAGE_SNAP_BY_ORDINAL32(names[index].u1.Ordinal)) continue;
            const IMAGE_IMPORT_BY_NAME *entry = (const IMAGE_IMPORT_BY_NAME *)RvaToFile(file, size, nt, names[index].u1.AddressOfData);
            if (entry != NULL && strcmp((const char *)entry->Name, name) == 0) {
                *slot_address = SWORDMAN_IMAGE_BASE + imports->FirstThunk + index * sizeof(uint32_t);
                found = true;
                goto done;
            }
        }
    }
done:
    HeapFree(GetProcessHeap(), 0, file);
    return found;
}

static bool CreateGame(const wchar_t *path, PROCESS_INFORMATION *info) {
    wchar_t directory[MAX_PATH];
    if (wcsncpy_s(directory, _countof(directory), path, _TRUNCATE) != 0) return false;
    wchar_t *slash = wcsrchr(directory, L'\\');
    if (slash == NULL) return false;
    *slash = L'\0';
    STARTUPINFOW startup = {sizeof(startup)};
    ZeroMemory(info, sizeof(*info));
    return CreateProcessW(path, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, directory,
                          &startup, info) != FALSE;
}

static bool InjectRuntime(HANDLE process, const wchar_t *dll_path,
                          const SwordmanOptions *options, const wchar_t *game_path) {
    uint32_t get_module_slot, get_proc_slot;
    if (!FindImportSlot(game_path, "KERNEL32.dll", "GetModuleHandleA", &get_module_slot) ||
        !FindImportSlot(game_path, "KERNEL32.dll", "GetProcAddress", &get_proc_slot)) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return false;
    }
    uint8_t data[STUB_DATA_SIZE] = {0};
    size_t path_bytes = (wcslen(dll_path) + 1u) * sizeof(wchar_t);
    if (path_bytes > STUB_KERNEL) {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return false;
    }
    memcpy(data + STUB_PATH, dll_path, path_bytes);
    memcpy(data + STUB_KERNEL, "kernel32.dll", sizeof("kernel32.dll"));
    memcpy(data + STUB_LOAD, "LoadLibraryW", sizeof("LoadLibraryW"));
    memcpy(data + STUB_START, "SwordmanRuntimeStart", sizeof("SwordmanRuntimeStart"));
    memcpy(data + STUB_OPTIONS, options, sizeof(*options));
    *(uint32_t *)(data + STUB_GETMODULE) = get_module_slot;
    *(uint32_t *)(data + STUB_GETPROC) = get_proc_slot;

    LPVOID remote_data = VirtualAllocEx(process, NULL, sizeof(data), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    LPVOID remote_code = VirtualAllocEx(process, NULL, kRuntimeLoaderStubSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (remote_data == NULL || remote_code == NULL || (uintptr_t)remote_code > UINT32_MAX) {
        if (remote_data != NULL) VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        if (remote_code != NULL) VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
    }
    SIZE_T written;
    bool success = WriteProcessMemory(process, remote_data, data, sizeof(data), &written) && written == sizeof(data) &&
                   WriteProcessMemory(process, remote_code, kRuntimeLoaderStub, kRuntimeLoaderStubSize, &written) &&
                   written == kRuntimeLoaderStubSize;
    DWORD old_protection;
    success = success && VirtualProtectEx(process, remote_code, kRuntimeLoaderStubSize, PAGE_EXECUTE_READ, &old_protection);
    FlushInstructionCache(process, remote_code, kRuntimeLoaderStubSize);
    HANDLE thread = success ? CreateRemoteThread(process, NULL, 0,
        (LPTHREAD_START_ROUTINE)(uintptr_t)remote_code, remote_data, 0, NULL) : NULL;
    if (thread == NULL) {
        VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
        return false;
    }
    DWORD wait = WaitForSingleObject(thread, 10000), code = 0;
    bool thread_done = wait == WAIT_OBJECT_0;
    if (!thread_done) {
        /* The stub may still be executing from remote_code. Kill the new
           process and wait for that thread before releasing its mappings. */
        TerminateProcess(process, 1);
        thread_done = WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0;
    }
    bool got_exit_code = thread_done && GetExitCodeThread(thread, &code);
    success = got_exit_code && code == ERROR_SUCCESS;
    DWORD error = success ? ERROR_SUCCESS : (wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_DLL_INIT_FAILED);
    if (got_exit_code && code != ERROR_SUCCESS) error = code;
    CloseHandle(thread);
    if (thread_done) {
        VirtualFreeEx(process, remote_data, 0, MEM_RELEASE);
        VirtualFreeEx(process, remote_code, 0, MEM_RELEASE);
    }
    SetLastError(error);
    return success;
}

static void SetStatus(const wchar_t *message) {
    SetWindowTextW(g_status, message);
}

static void ShowError(HWND owner, const wchar_t *action) {
    DWORD error = GetLastError();
    wchar_t system_message[512] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, error, 0, system_message, _countof(system_message), NULL);
    wchar_t message[768];
    _snwprintf_s(message, _countof(message), _TRUNCATE,
                 L"%ls失败（错误 %lu）：%ls", action, (unsigned long)error, system_message);
    MessageBoxW(owner, message, L"天地劫：神魔至尊传补丁工具", MB_OK | MB_ICONERROR);
}

static HWND AddControl(HWND parent, const wchar_t *class_name, const wchar_t *text,
                       DWORD style, int x, int y, int width, int height, int id) {
    HWND control = CreateWindowExW(0, class_name, text, WS_CHILD | WS_VISIBLE | style,
                                   x, y, width, height, parent, (HMENU)(INT_PTR)id,
                                   g_instance, NULL);
    SendMessageW(control, WM_SETFONT, (WPARAM)g_font, TRUE);
    return control;
}

static bool SelectGamePath(HWND owner, wchar_t *path, size_t capacity) {
    if (LoadSavedGamePath(path, capacity) || ResolveRelativeGamePath(path, capacity)) {
        return true;
    }

    path[0] = L'\0';
    OPENFILENAMEW dialog = {sizeof(dialog)};
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Sm_win2.exe\0Sm_win2.exe\0可执行文件\0*.exe\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = (DWORD)capacity;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) return false;

    wchar_t full_path[MAX_PATH];
    DWORD length = GetFullPathNameW(path, _countof(full_path), full_path, NULL);
    if (length == 0 || length >= _countof(full_path) || !ExistingFile(full_path)) return false;
    if (wcsncpy_s(path, capacity, full_path, _TRUNCATE) != 0) return false;
    SaveGamePath(path);
    return true;
}

static bool IsChecked(int control_id) {
    return IsDlgButtonChecked(g_window, control_id) == BST_CHECKED;
}

static SwordmanOptions ReadOptions(void) {
    SwordmanOptions options = {0, SWORDMAN_GROWTH_NORMAL};
    const int ids[] = {
        ID_NO_CD, ID_SPEED, ID_CHESTS, ID_ATTRIBUTES, ID_CAST_AFTER_MOVE,
        ID_SUMMON_ITEM_COPY, ID_SAVE_ANYTIME, ID_RESIZABLE, ID_MOVIE,
        ID_SMOOTH, ID_CLEANUP, ID_SOFTWARE_MOVIE,
    };
    const uint32_t flags[] = {
        SWORDMAN_NO_CD, SWORDMAN_SPEED, SWORDMAN_CHESTS, SWORDMAN_ATTRIBUTE_999,
        SWORDMAN_CAST_AFTER_MOVE, SWORDMAN_SUMMON_ITEM_COPY, SWORDMAN_SAVE_ANYTIME,
        SWORDMAN_RESIZABLE, SWORDMAN_MOVIE, SWORDMAN_SMOOTH,
        SWORDMAN_MOVIE_CLEANUP, SWORDMAN_SOFTWARE_MOVIE,
    };
    for (size_t index = 0; index < _countof(ids); ++index) {
        if (IsChecked(ids[index])) options.flags |= flags[index];
    }
    if (IsChecked(ID_GROWTH_MAX)) options.growth = SWORDMAN_GROWTH_MAXIMUM;
    if (IsChecked(ID_GROWTH_MIN)) options.growth = SWORDMAN_GROWTH_MINIMUM;
    return options;
}

static void UpdateMovieControls(HWND window) {
    bool movie = IsChecked(ID_MOVIE);
    EnableWindow(GetDlgItem(window, ID_SOFTWARE_MOVIE), movie);
    if (!movie) CheckDlgButton(window, ID_SOFTWARE_MOVIE, BST_UNCHECKED);
    InvalidateRect(window, NULL, TRUE);
}

static void Launch(HWND owner) {
    wchar_t path[MAX_PATH];
    if (!SelectGamePath(owner, path, _countof(path))) {
        SetStatus(L"未选择可启动的 Sm_win2.exe。等待启动。");
        return;
    }

    SwordmanOptions options = ReadOptions();
    SaveOptions(&options);
    if (!SwordmanHashFile(path, kSwordmanImageHash)) {
        SetLastError(ERROR_REVISION_MISMATCH);
        ShowError(owner, L"校验 Sm_win2.exe");
        return;
    }

    wchar_t directory[MAX_PATH], dll_path[MAX_PATH];
    if (!LauncherDirectory(directory, _countof(directory)) ||
        wcscpy_s(dll_path, _countof(dll_path), directory) != 0 ||
        wcscat_s(dll_path, _countof(dll_path), L"SwordmanRuntime.dll") != 0 ||
        !ExistingFile(dll_path)) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        ShowError(owner, L"查找 SwordmanRuntime.dll");
        return;
    }

    PROCESS_INFORMATION info;
    if (!CreateGame(path, &info)) {
        ShowError(owner, L"创建已挂起的游戏进程");
        return;
    }
    bool success = InjectRuntime(info.hProcess, dll_path, &options, path);
    if (success && ResumeThread(info.hThread) != (DWORD)-1) {
        wchar_t status[160];
        _snwprintf_s(status, _countof(status), _TRUNCATE,
                     L"已向 PID %lu 写入所选补丁并启动游戏。原始 Sm_win2.exe 未被修改。",
                     (unsigned long)info.dwProcessId);
        SetStatus(status);
        CloseHandle(info.hThread);
        CloseHandle(info.hProcess);
        return;
    }

    DWORD error = GetLastError();
    TerminateProcess(info.hProcess, 1);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    SetLastError(error);
    ShowError(owner, L"注入 SwordmanRuntime.dll");
}

static void CreateControls(HWND window) {
    g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    AddControl(window, L"STATIC", L"游戏补丁", SS_LEFT, 18, 18, 140, 22, -1);
    AddControl(window, L"BUTTON", L"免 CD", BS_AUTOCHECKBOX, 18, 44, 300, 24, ID_NO_CD);
    AddControl(window, L"BUTTON", L"加速行动", BS_AUTOCHECKBOX, 18, 70, 300, 24, ID_SPEED);
    AddControl(window, L"BUTTON", L"显示全部宝箱", BS_AUTOCHECKBOX, 18, 96, 300, 24, ID_CHESTS);
    AddControl(window, L"BUTTON", L"属性上限 999", BS_AUTOCHECKBOX, 18, 122, 300, 24, ID_ATTRIBUTES);
    AddControl(window, L"BUTTON", L"移动后可施法", BS_AUTOCHECKBOX, 18, 148, 300, 24, ID_CAST_AFTER_MOVE);
    AddControl(window, L"BUTTON", L"恢复召唤兽复制物品特性", BS_AUTOCHECKBOX, 18, 174, 320, 24, ID_SUMMON_ITEM_COPY);
    AddControl(window, L"BUTTON", L"随时中途存档", BS_AUTOCHECKBOX, 18, 200, 300, 24, ID_SAVE_ANYTIME);
    AddControl(window, L"STATIC", L"成长方式", SS_LEFT, 18, 232, 140, 22, -1);
    AddControl(window, L"BUTTON", L"正常成长", BS_AUTORADIOBUTTON | WS_GROUP, 18, 258, 100, 24, ID_GROWTH_NORMAL);
    AddControl(window, L"BUTTON", L"最大成长", BS_AUTORADIOBUTTON, 126, 258, 100, 24, ID_GROWTH_MAX);
    AddControl(window, L"BUTTON", L"最小成长", BS_AUTORADIOBUTTON, 234, 258, 100, 24, ID_GROWTH_MIN);

    AddControl(window, L"STATIC", L"显示模式", SS_LEFT, 360, 18, 140, 22, -1);
    AddControl(window, L"BUTTON", L"窗口模式", BS_AUTOCHECKBOX, 360, 44, 220, 24, ID_RESIZABLE);
    AddControl(window, L"BUTTON", L"电影缩放", BS_AUTOCHECKBOX, 360, 70, 220, 24, ID_MOVIE);
    AddControl(window, L"BUTTON", L"平滑电影画面", BS_AUTOCHECKBOX, 360, 96, 220, 24, ID_SMOOTH);
    AddControl(window, L"BUTTON", L"电影播放清理", BS_AUTOCHECKBOX, 360, 122, 220, 24, ID_CLEANUP);
    AddControl(window, L"BUTTON", L"软件电影解码", BS_AUTOCHECKBOX, 360, 148, 220, 24, ID_SOFTWARE_MOVIE);
    AddControl(window, L"STATIC",
               L"启动时创建已挂起的进程，仅向该进程内存写入补丁。",
               SS_LEFT, 18, 330, 552, 22, -1);
    AddControl(window, L"BUTTON", L"应用补丁并启动游戏", BS_DEFPUSHBUTTON,
               360, 360, 210, 32, ID_LAUNCH);
    g_status = AddControl(window, L"STATIC", L"等待启动。", SS_LEFT,
                          18, 368, 330, 40, ID_STATUS);

    const struct {
        int id;
        const wchar_t *key;
    } checks[] = {
        {ID_NO_CD, L"NoCd"},
        {ID_SPEED, L"Speed"},
        {ID_CHESTS, L"Chests"},
        {ID_ATTRIBUTES, L"Attributes"},
        {ID_CAST_AFTER_MOVE, L"CastAfterMove"},
        {ID_SUMMON_ITEM_COPY, L"SummonItemCopy"},
        {ID_SAVE_ANYTIME, L"SaveAnytime"},
        {ID_RESIZABLE, L"Resizable"},
        {ID_MOVIE, L"Movie"},
        {ID_SMOOTH, L"Smooth"},
        {ID_CLEANUP, L"MovieCleanup"},
        {ID_SOFTWARE_MOVIE, L"SoftwareMovie"},
    };
    for (size_t index = 0; index < _countof(checks); ++index) {
        CheckDlgButton(window, checks[index].id,
                       LoadConfigBool(checks[index].key, false) ? BST_CHECKED : BST_UNCHECKED);
    }
    uint32_t growth = LoadConfigGrowth();
    int growth_id = growth == SWORDMAN_GROWTH_MAXIMUM ? ID_GROWTH_MAX :
                    growth == SWORDMAN_GROWTH_MINIMUM ? ID_GROWTH_MIN : ID_GROWTH_NORMAL;
    CheckDlgButton(window, growth_id, BST_CHECKED);
    UpdateMovieControls(window);
}

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    (void)l_param;
    switch (message) {
        case WM_CREATE:
            g_window = window;
            CreateControls(window);
            return 0;
        case WM_COMMAND:
            if (HIWORD(w_param) == BN_CLICKED) {
                if (LOWORD(w_param) == ID_MOVIE) {
                    UpdateMovieControls(window);
                    return 0;
                }
                if (LOWORD(w_param) == ID_LAUNCH) {
                    Launch(window);
                    return 0;
                }
            }
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)w_param;
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        case WM_CTLCOLORBTN: {
            HDC dc = (HDC)w_param;
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, w_param, l_param);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show) {
    (void)previous;
    (void)command_line;
    g_instance = instance;

    INITCOMMONCONTROLSEX common_controls = {
        sizeof(common_controls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES
    };
    InitCommonControlsEx(&common_controls);

    WNDCLASSW klass;
    ZeroMemory(&klass, sizeof(klass));
    klass.hInstance = instance;
    klass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    klass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    klass.lpszClassName = L"SwordmanPatchesWindow";
    klass.lpfnWndProc = WindowProc;
    if (RegisterClassW(&klass) == 0) return 1;

    HWND window = CreateWindowExW(WS_EX_APPWINDOW,
                                  klass.lpszClassName,
                                  L"《天地劫：神魔至尊传》补丁工具",
                                  WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                  0, 0, 604, 460, NULL, NULL, instance, NULL);
    if (window == NULL) return 1;
    g_window = window;

    RECT work_area = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    RECT window_rect = {0};
    GetWindowRect(window, &window_rect);
    int width = window_rect.right - window_rect.left;
    int height = window_rect.bottom - window_rect.top;
    int x = work_area.left + ((work_area.right - work_area.left) - width) / 2;
    int y = work_area.top + ((work_area.bottom - work_area.top) - height) / 2;
    SetWindowPos(window, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}
