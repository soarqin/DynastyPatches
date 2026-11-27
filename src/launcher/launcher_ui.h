#ifndef LAUNCHER_UI_H
#define LAUNCHER_UI_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const wchar_t *name;
    const wchar_t *hint;
} LauncherPage;

typedef struct {
    uint32_t first, last;
    const wchar_t *const *labels; /* NULL for numeric multipliers. */
    const uint32_t *ini_values; /* Optional INI values in display order. */
} LauncherRange;

typedef struct LauncherOption {
    int page;
    const wchar_t *label;
    const wchar_t *section;
    const wchar_t *key;
    uint32_t default_value;
    const LauncherRange *range; /* NULL for a checkbox. */
    const struct LauncherOption *depends_on;
    bool clear_when_disabled;
    uint32_t value; /* UI value in the range, translated on load and save if mapped. */
} LauncherOption;

typedef struct {
    const wchar_t *window_class;
    const wchar_t *title;
    const wchar_t *game;
    const wchar_t *version_label;
    const LauncherPage *pages;
    int page_count;
    LauncherOption *options;
    int option_count;
    bool (*config_path)(wchar_t *path, size_t capacity);
    void (*launch)(HWND owner);
} LauncherSpec;

int LauncherRun(HINSTANCE instance, int show, const LauncherSpec *spec);
void LauncherSetStatus(const wchar_t *message);
void LauncherSaveOptions(void);

#endif
