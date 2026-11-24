#include "launcher_ui.h"

#include <commctrl.h>
#include <stdio.h>

enum {
    TITLE = 10, GAME, VERSION, PAGE_TITLE, HINT, STATUS, KEYS,
    NAV = 100, ROW = 200, LESS = 300, MORE = 400, ACTION = 500
};
enum { START, SAVE, RESET, EXIT_APP, ACTION_COUNT };
enum { MARGIN = 20, NAV_WIDTH = 150, DIVIDER_X = 185, CONTENT_X = 202, ROW_TOP = 125 };
static const wchar_t *const actions[] = {L"启动游戏", L"仅保存", L"恢复默认", L"退出"};

static const COLORREF canvas = RGB(24, 31, 37), panel = RGB(35, 44, 51),
    rule = RGB(74, 88, 92), highlight = RGB(49, 66, 70), ink = RGB(236, 231, 211),
    muted = RGB(174, 187, 180), gold = RGB(238, 195, 118), mint = RGB(128, 199, 169),
    disabled = RGB(140, 151, 148);

static struct {
    const LauncherSpec *spec;
    HWND window;
    wchar_t ini[MAX_PATH];
    int page, dpi;
    HFONT body, small, heading, title;
} ui;

static HWND Child(int id) {
    return GetDlgItem(ui.window, id);
}

static int Px(int logical) {
    return MulDiv(logical, ui.dpi, 96);
}

static bool Enabled(const LauncherOption *option) {
    return !option->depends_on || option->depends_on->value != 0;
}

static void LoadOptions(void) {
    for (int i = 0; i < ui.spec->option_count; ++i) {
        LauncherOption *option = &ui.spec->options[i];
        uint32_t value = option->default_value;
        if (ui.ini[0])
            value = GetPrivateProfileIntW(option->section, option->key, (INT)value, ui.ini);
        const LauncherRange *range = option->range;
        if (!range) value = value != 0;
        else if (value < range->first || value > range->last) {
            /* Legacy multiplier edits clamp; an unknown choice falls back to its default. */
            value = range->labels ? option->default_value :
                    value < range->first ? range->first : range->last;
        }
        option->value = value;
    }
}

void LauncherSetStatus(const wchar_t *message) {
    SetWindowTextW(Child(STATUS), message);
}

void LauncherSaveOptions(void) {
    for (int i = 0; i < ui.spec->option_count; ++i) {
        const LauncherOption *option = &ui.spec->options[i];
        wchar_t number[16];
        swprintf_s(number, ARRAYSIZE(number), L"%u", option->value);
        if (!ui.ini[0] || !WritePrivateProfileStringW(option->section, option->key, number, ui.ini)) {
            LauncherSetStatus(L"无法保存设置，请检查启动器所在目录的写入权限。");
            return;
        }
    }
    LauncherSetStatus(L"设置已保存。");
}

static const wchar_t *ValueText(const LauncherRange *range, uint32_t value, wchar_t number[16]) {
    if (range->labels) return range->labels[value - range->first];
    swprintf_s(number, 16, L"%u 倍", value);
    return number;
}

static void RefreshOptions(void) {
    for (int i = 0; i < ui.spec->option_count; ++i) {
        LauncherOption *option = &ui.spec->options[i];
        bool enabled = Enabled(option);
        if (!enabled && option->clear_when_disabled) option->value = option->default_value;
        HWND row = Child(ROW + i);
        EnableWindow(row, enabled);
        if (option->range) {
            wchar_t name[128], number[16];
            swprintf_s(name, ARRAYSIZE(name), L"%ls　%ls", option->label,
                       ValueText(option->range, option->value, number));
            SetWindowTextW(row, name);
            EnableWindow(Child(LESS + i), enabled && option->value > option->range->first);
            EnableWindow(Child(MORE + i), enabled && option->value < option->range->last);
        } else {
            SendMessageW(row, BM_SETCHECK, option->value ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        InvalidateRect(row, NULL, FALSE);
    }
}

static void SetValue(int index, uint32_t value) {
    LauncherOption *option = &ui.spec->options[index];
    if (!Enabled(option) || option->value == value) return;
    option->value = value;
    RefreshOptions();
    LauncherSetStatus(L"设置已更改，尚未保存。");
}

static void StepValue(int index, int step) {
    const LauncherOption *option = &ui.spec->options[index];
    const LauncherRange *range = option->range;
    if (!range) SetValue(index, step > 0);
    else if (step < 0 && option->value > range->first) SetValue(index, option->value - 1);
    else if (step > 0 && option->value < range->last) SetValue(index, option->value + 1);
}

static void ChooseValue(int index) {
    const LauncherOption *option = &ui.spec->options[index];
    const LauncherRange *range = option->range;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    for (uint32_t value = range->first; value <= range->last; ++value) {
        wchar_t number[16];
        AppendMenuW(menu, MF_STRING, value - range->first + 1, ValueText(range, value, number));
    }
    CheckMenuRadioItem(menu, 1, range->last - range->first + 1,
                       option->value - range->first + 1, MF_BYCOMMAND);
    RECT rect;
    GetWindowRect(Child(ROW + index), &rect);
    int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN,
                                rect.right, rect.bottom, 0, ui.window, NULL);
    DestroyMenu(menu);
    if (choice) SetValue(index, range->first + (uint32_t)choice - 1);
}

static bool Add(int id, const wchar_t *class_name, const wchar_t *text, DWORD style) {
    return CreateWindowExW(0, class_name, text, WS_CHILD | WS_VISIBLE | style,
                           0, 0, 0, 0, ui.window, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(NULL), NULL) != NULL;
}

static bool CreateControls(void) {
    if (!Add(TITLE, L"STATIC", L"启动设置", SS_CENTERIMAGE) ||
        !Add(GAME, L"STATIC", ui.spec->game, SS_CENTERIMAGE) ||
        !Add(VERSION, L"STATIC", ui.spec->version_label, SS_RIGHT | SS_CENTERIMAGE) ||
        !Add(PAGE_TITLE, L"STATIC", L"", SS_CENTERIMAGE) ||
        !Add(HINT, L"STATIC", L"", SS_LEFT | SS_NOPREFIX) ||
        !Add(STATUS, L"STATIC", L"等待启动。", SS_LEFT | SS_NOPREFIX) ||
        !Add(KEYS, L"STATIC", L"Tab 切换控件     ↑ ↓ 选择     ← → 调整     Enter 确定     PgUp / PgDn 翻页", SS_CENTERIMAGE))
        return false;
    for (int i = 0; i < ui.spec->page_count; ++i)
        if (!Add(NAV + i, L"BUTTON", ui.spec->pages[i].name, BS_PUSHBUTTON | WS_TABSTOP))
            return false;
    for (int i = 0; i < ui.spec->option_count; ++i) {
        const LauncherOption *option = &ui.spec->options[i];
        DWORD type = option->range ? BS_PUSHBUTTON : BS_AUTOCHECKBOX;
        if (!Add(ROW + i, L"BUTTON", option->label, type | WS_TABSTOP)) return false;
        if (option->range) {
            wchar_t name[96];
            swprintf_s(name, ARRAYSIZE(name), L"上一个%ls", option->label);
            if (!Add(LESS + i, L"BUTTON", name, BS_PUSHBUTTON)) return false;
            swprintf_s(name, ARRAYSIZE(name), L"下一个%ls", option->label);
            if (!Add(MORE + i, L"BUTTON", name, BS_PUSHBUTTON)) return false;
        }
    }
    for (int i = 0; i < ACTION_COUNT; ++i)
        if (!Add(ACTION + i, L"BUTTON", actions[i], BS_PUSHBUTTON | WS_TABSTOP)) return false;
    return true;
}

static HFONT MakeFont(int size, int weight) {
    return CreateFontW(-Px(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, L"Microsoft YaHei UI");
}

static void FreeFonts(void) {
    DeleteObject(ui.body);
    DeleteObject(ui.small);
    DeleteObject(ui.heading);
    DeleteObject(ui.title);
}

static void SetFonts(void) {
    ui.body = MakeFont(16, FW_NORMAL);
    ui.small = MakeFont(14, FW_NORMAL);
    ui.heading = MakeFont(17, FW_SEMIBOLD);
    ui.title = MakeFont(30, FW_SEMIBOLD);
    for (HWND child = GetWindow(ui.window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        int id = GetDlgCtrlID(child);
        HFONT font = ui.body;
        if (id == TITLE) font = ui.title;
        else if (id == PAGE_TITLE) font = ui.heading;
        else if (id == KEYS || id == VERSION || id == HINT || id == STATUS) font = ui.small;
        SendMessageW(child, WM_SETFONT, (WPARAM)font, FALSE);
    }
}

static void Place(int id, int x, int y, int w, int h) {
    MoveWindow(Child(id), Px(x), Px(y), Px(x + w) - Px(x), Px(y + h) - Px(y), TRUE);
}

static void Layout(void) {
    RECT client;
    GetClientRect(ui.window, &client);
    int width = MulDiv(client.right, 96, ui.dpi);
    int height = MulDiv(client.bottom, 96, ui.dpi);
    int right = width - MARGIN, footer = height - 91;
    int row_step = (footer - 84 - ROW_TOP) / 6;
    if (row_step > 40) row_step = 40;
    Place(TITLE, MARGIN, 14, 145, 42);
    Place(GAME, 174, 20, right - 174 - 200, 36);
    Place(VERSION, right - 200, 20, 200, 36);
    for (int i = 0; i < ui.spec->page_count; ++i)
        Place(NAV + i, MARGIN, 88 + i * 43, NAV_WIDTH, 36);
    Place(PAGE_TITLE, CONTENT_X + 4, 85, right - CONTENT_X - 4, 32);
    int y = ROW_TOP;
    for (int i = 0; i < ui.spec->option_count; ++i) {
        const LauncherOption *option = &ui.spec->options[i];
        bool visible = option->page == ui.page;
        int end = right - (option->range ? 72 : 0);
        if (visible) {
            Place(ROW + i, CONTENT_X, y, end - CONTENT_X, row_step - 4);
            if (option->range) {
                Place(LESS + i, end + 1, y, 35, row_step - 4);
                Place(MORE + i, end + 37, y, 35, row_step - 4);
            }
            y += row_step;
        }
        ShowWindow(Child(ROW + i), visible ? SW_SHOWNA : SW_HIDE);
        if (option->range) {
            ShowWindow(Child(LESS + i), visible ? SW_SHOWNA : SW_HIDE);
            ShowWindow(Child(MORE + i), visible ? SW_SHOWNA : SW_HIDE);
        }
    }
    Place(HINT, CONTENT_X + 4, y + 7, right - CONTENT_X - 4, 37);
    Place(STATUS, CONTENT_X + 4, footer - 37, right - CONTENT_X - 4, 36);
    Place(KEYS, MARGIN + 2, footer + 3, right - MARGIN - 2, 30);
    for (int i = 0; i < ACTION_COUNT; ++i) {
        int span = right - MARGIN + 8;
        int x = MARGIN + i * span / ACTION_COUNT;
        int end = MARGIN + (i + 1) * span / ACTION_COUNT - 8;
        Place(ACTION + i, x, footer + 40, end - x, 36);
    }
    InvalidateRect(ui.window, NULL, FALSE);
}

static void OpenPage(int page) {
    ui.page = page;
    SetWindowTextW(Child(PAGE_TITLE), ui.spec->pages[page].name);
    SetWindowTextW(Child(HINT), ui.spec->pages[page].hint);
    Layout();
    for (int i = 0; i < ui.spec->page_count; ++i) InvalidateRect(Child(NAV + i), NULL, FALSE);
}

static void Fill(HDC dc, RECT rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, (HBRUSH)GetStockObject(DC_BRUSH));
}

static void Text(HDC dc, RECT rect, COLORREF color, UINT align, const wchar_t *text) {
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static void DrawButton(const NMCUSTOMDRAW *draw) {
    HDC dc = draw->hdc;
    HWND button = draw->hdr.hwndFrom;
    int id = GetDlgCtrlID(button);
    RECT box;
    GetClientRect(button, &box);
    bool focus = GetFocus() == button;
    bool hot = (draw->uItemState & (CDIS_HOT | CDIS_SELECTED)) != 0;
    bool enabled = IsWindowEnabled(button) != FALSE;
    bool active_page = id == NAV + ui.page;
    COLORREF bg = panel, fg = ink;
    if (id < ROW) { bg = canvas; fg = muted; }
    if (active_page || (enabled && (hot || focus))) { bg = highlight; fg = ink; }
    if (id == ACTION + START) { bg = mint; fg = canvas; }
    if (!enabled) fg = disabled;
    int saved = SaveDC(dc);
    Fill(dc, box, bg);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, ui.body);
    RECT label = box;
    label.left += Px(14);
    label.right -= Px(12);
    if (id >= ACTION) {
        Text(dc, label, fg, DT_LEFT, actions[id - ACTION]);
    } else if (id >= LESS) {
        Text(dc, box, enabled ? gold : disabled, DT_CENTER, id >= MORE ? L"+" : L"−");
    } else if (id >= ROW) {
        const LauncherOption *option = &ui.spec->options[id - ROW];
        RECT value = label;
        wchar_t number[16];
        const wchar_t *text;
        if (option->range) {
            text = ValueText(option->range, option->value, number);
        } else {
            text = option->value ? L"开启" : L"关闭";
            int x = label.right - Px(28), y = (box.bottom - Px(14)) / 2;
            COLORREF track = enabled && option->value ? mint : rule;
            Fill(dc, (RECT){x, y, label.right, y + Px(14)}, track);
            x += Px(option->value ? 17 : 3);
            Fill(dc, (RECT){x, y + Px(3), x + Px(8), y + Px(11)}, option->value && enabled ? canvas : muted);
            value.right -= Px(38);
        }
        SIZE size;
        GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
        value.left = value.right - size.cx;
        label.right = value.left - Px(8);
        Text(dc, value, !enabled ? disabled : !option->range && option->value ? mint : muted, DT_RIGHT, text);
        Text(dc, label, focus ? gold : fg, DT_LEFT, option->label);
    } else {
        Text(dc, label, fg, DT_LEFT, ui.spec->pages[id - NAV].name);
    }
    if (focus || active_page) {
        RECT stripe = box;
        if (id >= ACTION) stripe.top = stripe.bottom - Px(3);
        else stripe.right = Px(3);
        Fill(dc, stripe, focus ? gold : mint);
    }
    RestoreDC(dc, saved);
}

static void ButtonClicked(int id) {
    if (id >= ACTION && id < ACTION + ACTION_COUNT) {
        switch (id - ACTION) {
        case START: ui.spec->launch(ui.window); break;
        case SAVE: LauncherSaveOptions(); break;
        case RESET:
            for (int i = 0; i < ui.spec->option_count; ++i)
                ui.spec->options[i].value = ui.spec->options[i].default_value;
            RefreshOptions();
            LauncherSetStatus(L"已恢复默认设置，选择「仅保存」或「启动游戏」后写入配置。");
            break;
        case EXIT_APP: DestroyWindow(ui.window); break;
        }
    } else if ((id >= LESS && id < LESS + ui.spec->option_count) ||
               (id >= MORE && id < MORE + ui.spec->option_count)) {
        int index = id >= MORE ? id - MORE : id - LESS;
        SetFocus(Child(ROW + index));
        StepValue(index, id >= MORE ? 1 : -1);
    } else if (id >= ROW && id < ROW + ui.spec->option_count) {
        int i = id - ROW;
        if (ui.spec->options[i].range) ChooseValue(i);
        else SetValue(i, SendMessageW(Child(id), BM_GETCHECK, 0, 0) == BST_CHECKED);
    } else if (id >= NAV && id < NAV + ui.spec->page_count) {
        OpenPage(id - NAV);
    }
}

static bool HandleKey(const MSG *msg) {
    if (msg->message != WM_KEYDOWN) return false;
    int key = (int)msg->wParam, id = GetDlgCtrlID(msg->hwnd);
    int step = key == VK_PRIOR || key == VK_UP || key == VK_LEFT ? -1 : 1;
    bool nav = id >= NAV && id < NAV + ui.spec->page_count;
    if (key == VK_PRIOR || key == VK_NEXT || (nav && (key == VK_UP || key == VK_DOWN))) {
        int count = ui.spec->page_count;
        int page = (ui.page + count + step) % count;
        SetFocus(Child(NAV + page));
        OpenPage(page);
    } else if (id >= ROW && id < ROW + ui.spec->option_count && (key == VK_LEFT || key == VK_RIGHT)) {
        StepValue(id - ROW, step);
    } else if (id >= ROW && id < ROW + ui.spec->option_count && (key == VK_UP || key == VK_DOWN)) {
        int count = ui.spec->option_count, i = id - ROW;
        do { i = (i + count + step) % count; }
        while (ui.spec->options[i].page != ui.page || !Enabled(&ui.spec->options[i]));
        SetFocus(Child(ROW + i));
    } else if (id >= ACTION && (key == VK_LEFT || key == VK_RIGHT)) {
        SetFocus(Child(ACTION + (id - ACTION + ACTION_COUNT + step) % ACTION_COUNT));
    } else if (key == VK_RETURN && id >= NAV) {
        if (!(msg->lParam & (1L << 30))) SendMessageW(msg->hwnd, BM_CLICK, 0, 0);
    } else if (key == VK_ESCAPE) {
        DestroyWindow(ui.window);
    } else return false;
    return true;
}

static LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        ui.window = window;
        if (!CreateControls()) return -1;
        SetFonts();
        RefreshOptions();
        OpenPage(0);
        return 0;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED && Child(ACTION + START)) Layout();
        return 0;
    case WM_GETMINMAXINFO: {
        RECT rect = {0, 0, Px(640), Px(480)};
        AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
        ((MINMAXINFO *)lp)->ptMinTrackSize = (POINT){rect.right - rect.left, rect.bottom - rect.top};
        return 0;
    }
    case WM_DPICHANGED: {
        ui.dpi = HIWORD(wp);
        FreeFonts();
        SetFonts();
        const RECT *r = (const RECT *)lp;
        SetWindowPos(window, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);
        RECT r;
        GetClientRect(window, &r);
        Fill(dc, r, canvas);
        int foot = r.bottom - Px(91);
        Fill(dc, (RECT){Px(MARGIN), Px(70), r.right - Px(MARGIN), Px(70) + 1}, rule);
        Fill(dc, (RECT){Px(MARGIN), foot, r.right - Px(MARGIN), foot + 1}, rule);
        Fill(dc, (RECT){Px(DIVIDER_X), Px(88), Px(DIVIDER_X) + 1, foot - Px(5)}, panel);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        int id = GetDlgCtrlID((HWND)lp);
        COLORREF fg = muted;
        if (id == TITLE || id == GAME) fg = ink;
        else if (id == PAGE_TITLE || id == STATUS) fg = gold;
        else if (id == HINT) fg = mint;
        SetTextColor(dc, fg);
        SetBkColor(dc, canvas);
        SetDCBrushColor(dc, canvas);
        return (LRESULT)GetStockObject(DC_BRUSH);
    }
    case WM_NOTIFY: {
        const NMCUSTOMDRAW *draw = (const NMCUSTOMDRAW *)lp;
        if (draw->hdr.code == NM_CUSTOMDRAW && draw->dwDrawStage == CDDS_PREPAINT) {
            DrawButton(draw);
            return CDRF_SKIPDEFAULT;
        }
        break;
    }
    case WM_COMMAND:
        if (lp && HIWORD(wp) == BN_CLICKED) ButtonClicked(LOWORD(wp));
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, msg, wp, lp);
}

int LauncherRun(HINSTANCE instance, int show, const LauncherSpec *spec) {
    ui.spec = spec;
    if (!spec->config_path(ui.ini, ARRAYSIZE(ui.ini))) ui.ini[0] = L'\0';
    LoadOptions();
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_STANDARD_CLASSES};
    if (!InitCommonControlsEx(&controls)) return 1;
    WNDCLASSW klass = {0};
    klass.hInstance = instance;
    klass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    klass.lpszClassName = spec->window_class;
    klass.lpfnWndProc = WindowProc;
    if (!RegisterClassW(&klass)) return 1;

    HDC dc = GetDC(NULL);
    ui.dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    RECT work, rect = {0, 0, Px(800), Px(540)};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    AdjustWindowRectEx(&rect, style, FALSE, 0);
    int width = rect.right - rect.left, height = rect.bottom - rect.top;
    if (width > work.right - work.left) width = work.right - work.left;
    if (height > work.bottom - work.top) height = work.bottom - work.top;
    HWND window = CreateWindowExW(0, spec->window_class, spec->title, style,
                                  work.left + (work.right - work.left - width) / 2,
                                  work.top + (work.bottom - work.top - height) / 2,
                                  width, height, NULL, NULL, instance, NULL);
    if (!window) { FreeFonts(); return 1; }
    ShowWindow(window, show);
    SetFocus(Child(NAV));
    UpdateWindow(window);
    MSG message;
    BOOL result;
    while ((result = GetMessageW(&message, NULL, 0, 0)) > 0) {
        if (HandleKey(&message) || IsDialogMessageW(window, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    FreeFonts();
    return result < 0 ? 1 : (int)message.wParam;
}
