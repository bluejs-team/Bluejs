/*
 * Win32 implementations of Blue.Window / Dialog / Clipboard APIs.
 * Implements every function declared in jsc_blue_native.h using Win32 directly.
 *
 * Thread safety: all Win32 GUI calls must run on the UI thread that owns
 * g_blue_host_hwnd. Cross-thread callers use SendMessage with WM_BLUE_DISPATCH
 * (defined in jsc_webview_win.cpp). SendMessage from a non-UI thread is
 * inherently synchronous - it blocks until the WndProc returns.
 */

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>   /* GetOpenFileName / GetSaveFileName */
#include <commctrl.h>  /* TASKDIALOGCONFIG, dynamically loaded TaskDialogIndirect */
#include <shlobj.h>    /* SHCreateMemStream (used in webview) */
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "../jsc_blue_native.h"

/* Defined in jsc_webview_win.cpp, set when the modal window is alive. */
extern HWND  g_blue_host_hwnd;
extern DWORD g_blue_ui_thread_id;

#define WM_BLUE_DISPATCH (WM_APP + 1)

struct BlueWinDispatch {
    void (*fn)(void*);
    void* arg;
};

/* Run fn(arg) on the UI thread.  If we're already on the UI thread (or no
 * window exists yet) call fn directly.  Otherwise SendMessage - synchronous. */
static void blue_run_ui(void (*fn)(void*), void* arg) {
    if (!g_blue_host_hwnd || GetCurrentThreadId() == g_blue_ui_thread_id) {
        fn(arg);
        return;
    }
    BlueWinDispatch d { fn, arg };
    SendMessageW(g_blue_host_hwnd, WM_BLUE_DISPATCH, 0, (LPARAM)&d);
}

/* ── UTF-8 ↔ wchar_t helpers ──────────────────────────────────────────── */

static std::wstring utf8_to_wide(const char* s) {
    if (!s || !s[0]) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

static char* wide_to_utf8_malloc(const wchar_t* w) {
    if (!w || !w[0]) return strdup("");
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return strdup("");
    char* s = (char*)malloc((size_t)n + 1);
    if (!s) return strdup("");
    int written = WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, nullptr, nullptr);
    if (written <= 0) { free(s); return strdup(""); }
    s[n] = '\0'; /* WideCharToMultiByte with -1 includes NUL, but be explicit */
    return s;
}

/* ── Window operations ────────────────────────────────────────────────── */

struct WinWJob { int op; std::wstring title; int w, h, flag; };

static void wjob_run(void* p) {
    WinWJob* j = (WinWJob*)p;
    HWND hw = g_blue_host_hwnd;
    if (!hw) return;
    switch (j->op) {
    case 0: /* set_title */
        SetWindowTextW(hw, j->title.c_str());
        break;
    case 1: { /* set_size */
        RECT r; GetWindowRect(hw, &r);
        MoveWindow(hw, r.left, r.top, j->w, j->h, TRUE);
        break;
    }
    case 2: { /* center */
        RECT r; GetWindowRect(hw, &r);
        int W = r.right - r.left, H = r.bottom - r.top;
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(hw, nullptr, (sw - W) / 2, (sh - H) / 2, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER);
        break;
    }
    case 3: { /* set_frameless */
        LONG style = GetWindowLongW(hw, GWL_STYLE);
        if (j->flag)
            style &= ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU);
        else
            style |= (WS_CAPTION | WS_THICKFRAME | WS_SYSMENU);
        SetWindowLongW(hw, GWL_STYLE, style);
        SetWindowPos(hw, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        break;
    }
    case 4: ShowWindow(hw, SW_MINIMIZE); break;
    case 5: ShowWindow(hw, SW_MAXIMIZE); break;
    case 6: PostMessageW(hw, WM_CLOSE, 0, 0); break;
    case 7:
        SetWindowPos(hw, j->flag ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        break;
    }
}

extern "C" void jsc_blue_window_set_title(const char* title_utf8) {
    WinWJob j { 0, utf8_to_wide(title_utf8), 0, 0, 0 };
    blue_run_ui(wjob_run, &j);
}
extern "C" void jsc_blue_window_set_size(int w, int h) {
    WinWJob j { 1, L"", w, h, 0 };
    blue_run_ui(wjob_run, &j);
}
extern "C" void jsc_blue_window_center(void) {
    WinWJob j { 2, L"", 0, 0, 0 };
    blue_run_ui(wjob_run, &j);
}
extern "C" void jsc_blue_window_set_frameless(int frameless_boolean) {
    WinWJob j { 3, L"", 0, 0, frameless_boolean };
    blue_run_ui(wjob_run, &j);
}
extern "C" void jsc_blue_window_minimize(void) {
    WinWJob j { 4, L"", 0, 0, 0 };
    blue_run_ui(wjob_run, &j);
}
extern "C" void jsc_blue_window_maximize(void) {
    WinWJob j { 5, L"", 0, 0, 0 };
    blue_run_ui(wjob_run, &j);
}
extern "C" void jsc_blue_window_close(void) {
    WinWJob j { 6, L"", 0, 0, 0 };
    blue_run_ui(wjob_run, &j);
}
extern "C" void jsc_blue_window_set_always_on_top(int on_boolean) {
    WinWJob j { 7, L"", 0, 0, on_boolean };
    blue_run_ui(wjob_run, &j);
}

/* ── File dialogs ─────────────────────────────────────────────────────── */

/* Build an OPENFILENAMEW-compatible filter string from a description and a
 * comma-separated list of patterns.  Format: "Desc\0*.ext;*.ext2\0\0" */
static std::wstring build_filter(const char* desc, const char* patterns_csv) {
    std::wstring f;
    std::wstring wdesc    = utf8_to_wide(desc && desc[0] ? desc : "Files");
    std::wstring wpat_raw = utf8_to_wide(patterns_csv && patterns_csv[0] ? patterns_csv : "*.*");
    /* Convert commas to semicolons for the OFN filter */
    std::wstring wpat;
    for (wchar_t c : wpat_raw) wpat += (c == L',') ? L';' : c;

    f += wdesc; f += L'\0';
    f += wpat;  f += L'\0';
    f += L'\0';
    return f;
}

struct WinOpenJob {
    std::wstring  title;
    int           multiple;
    std::wstring  filter;
    char*         result; /* malloc-owned output */
};

static void open_run(void* p) {
    WinOpenJob* job = (WinOpenJob*)p;
    job->result = strdup("");

    /* Large buffer for multi-select: dir + NUL + file1 + NUL + file2 + NUL + NUL */
    const int BUF = 65536;
    std::vector<wchar_t> buf(BUF, L'\0');

    OPENFILENAMEW ofn = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = g_blue_host_hwnd;
    ofn.lpstrFilter  = job->filter.c_str();
    ofn.lpstrFile    = buf.data();
    ofn.nMaxFile     = BUF;
    ofn.lpstrTitle   = job->title.empty() ? nullptr : job->title.c_str();
    ofn.Flags        = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
    if (job->multiple)
        ofn.Flags |= OFN_ALLOWMULTISELECT;

    if (!GetOpenFileNameW(&ofn)) return; /* cancelled */

    if (!job->multiple) {
        free(job->result);
        job->result = wide_to_utf8_malloc(buf.data());
        return;
    }

    /* Multi-select result: if only one file, buf is just the full path.
     * If multiple, format is: dir\0file1\0file2\0\0 */
    const wchar_t* wp = buf.data();
    std::wstring dir(wp);
    wp += dir.size() + 1;
    if (*wp == L'\0') {
        /* Single file returned even with OFN_ALLOWMULTISELECT */
        free(job->result);
        job->result = wide_to_utf8_malloc(dir.c_str());
        return;
    }

    std::string paths;
    while (*wp) {
        std::wstring name(wp);
        wp += name.size() + 1;
        std::wstring full = dir + L'\\' + name;
        if (!paths.empty()) paths += '|';
        char* u = wide_to_utf8_malloc(full.c_str());
        paths += u;
        free(u);
    }
    free(job->result);
    job->result = strdup(paths.c_str());
}

struct WinSaveJob {
    std::wstring title;
    std::wstring default_name;
    std::wstring filter;
    char*        result;
};

static void save_run(void* p) {
    WinSaveJob* job = (WinSaveJob*)p;
    job->result = strdup("");

    const int BUF = 4096;
    std::vector<wchar_t> buf(BUF, L'\0');
    if (!job->default_name.empty()) {
        wcsncpy(buf.data(), job->default_name.c_str(), BUF - 1);
        buf[BUF - 1] = L'\0';
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = g_blue_host_hwnd;
    ofn.lpstrFilter  = job->filter.c_str();
    ofn.lpstrFile    = buf.data();
    ofn.nMaxFile     = BUF;
    ofn.lpstrTitle   = job->title.empty() ? nullptr : job->title.c_str();
    ofn.Flags        = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;

    if (!GetSaveFileNameW(&ofn)) return;

    free(job->result);
    job->result = wide_to_utf8_malloc(buf.data());
}

extern "C" char* jsc_blue_dialog_open_files(const char* title_utf8, int multiple,
                                              const char* filter_desc,
                                              const char* filter_patterns_csv) {
    WinOpenJob job {
        utf8_to_wide(title_utf8),
        multiple,
        build_filter(filter_desc, filter_patterns_csv),
        nullptr
    };
    blue_run_ui(open_run, &job);
    return job.result ? job.result : strdup("");
}

extern "C" char* jsc_blue_dialog_save_file(const char* title_utf8,
                                             const char* default_name_utf8,
                                             const char* filter_patterns_csv) {
    WinSaveJob job {
        utf8_to_wide(title_utf8),
        utf8_to_wide(default_name_utf8),
        build_filter(nullptr, filter_patterns_csv),
        nullptr
    };
    blue_run_ui(save_run, &job);
    return job.result ? job.result : strdup("");
}

/* ── Message box ──────────────────────────────────────────────────────── */

struct WinMbJob {
    std::wstring              title;
    std::wstring              message;
    const char*               type_ascii; /* "info","warning","error","question" */
    std::vector<std::wstring> buttons;
    int                       result;     /* 0-based index or -1 */
};

static void mb_run(void* p) {
    WinMbJob* job = (WinMbJob*)p;
    job->result = -1;

    /* Map type to TaskDialog icon */
    PCWSTR icon = TD_INFORMATION_ICON;
    if (job->type_ascii) {
        if (strcmp(job->type_ascii, "warning")  == 0) icon = TD_WARNING_ICON;
        else if (strcmp(job->type_ascii, "error") == 0) icon = TD_ERROR_ICON;
        else if (strcmp(job->type_ascii, "question") == 0) icon = TD_INFORMATION_ICON;
    }

    /* Build TASKDIALOG_BUTTON array; IDs start at 100 */
    std::vector<TASKDIALOG_BUTTON> btns;
    for (int i = 0; i < (int)job->buttons.size(); i++)
        btns.push_back({ 100 + i, job->buttons[i].c_str() });

    if (btns.empty())
        btns.push_back({ 100, L"OK" });

    TASKDIALOGCONFIG cfg = {};
    cfg.cbSize          = sizeof(cfg);
    cfg.hwndParent      = g_blue_host_hwnd;
    cfg.pszWindowTitle  = job->title.empty() ? L"blue" : job->title.c_str();
    cfg.pszContent      = job->message.c_str();
    cfg.pszMainIcon     = icon;
    cfg.cButtons        = (UINT)btns.size();
    cfg.pButtons        = btns.data();
    cfg.nDefaultButton  = 100;

    int pressed = 0;
    typedef HRESULT (WINAPI *TaskDialogIndirectProc)(
        const TASKDIALOGCONFIG*, int*, int*, BOOL*);
    static TaskDialogIndirectProc pTaskDialogIndirect = []() -> TaskDialogIndirectProc {
        HMODULE h = LoadLibraryW(L"comctl32.dll");
        if (!h) return nullptr;
        return reinterpret_cast<TaskDialogIndirectProc>(
            GetProcAddress(h, "TaskDialogIndirect"));
    }();

    if (pTaskDialogIndirect &&
        SUCCEEDED(pTaskDialogIndirect(&cfg, &pressed, nullptr, nullptr))) {
        job->result = pressed - 100;
        return;
    }

    UINT flags = MB_OK;
    if (job->type_ascii) {
        if (strcmp(job->type_ascii, "warning") == 0) flags |= MB_ICONWARNING;
        else if (strcmp(job->type_ascii, "error") == 0) flags |= MB_ICONERROR;
        else if (strcmp(job->type_ascii, "question") == 0) flags |= MB_ICONQUESTION;
        else flags |= MB_ICONINFORMATION;
    } else {
        flags |= MB_ICONINFORMATION;
    }

    if (job->buttons.size() == 2)
        flags = (flags & ~MB_OK) | MB_OKCANCEL;

    int mb = MessageBoxW(g_blue_host_hwnd,
                         job->message.c_str(),
                         job->title.empty() ? L"blue" : job->title.c_str(),
                         flags);
    if (job->buttons.size() == 2) {
        if (mb == IDOK) job->result = 0;
        else if (mb == IDCANCEL) job->result = 1;
    } else if (mb == IDOK) {
        job->result = 0;
    }
}

extern "C" int jsc_blue_dialog_message_box(const char* title_utf8,
                                            const char* message_utf8,
                                            const char* dialog_type_ascii,
                                            const char* buttons_lines_utf8) {
    WinMbJob job;
    job.title      = utf8_to_wide(title_utf8);
    job.message    = utf8_to_wide(message_utf8);
    job.type_ascii = dialog_type_ascii;
    job.result     = -1;

    /* Parse newline-separated button labels */
    if (buttons_lines_utf8 && buttons_lines_utf8[0]) {
        std::string s(buttons_lines_utf8);
        size_t start = 0;
        while (start < s.size()) {
            size_t end = s.find('\n', start);
            if (end == std::string::npos) end = s.size();
            std::string label = s.substr(start, end - start);
            /* Trim carriage return */
            if (!label.empty() && label.back() == '\r') label.pop_back();
            if (!label.empty())
                job.buttons.push_back(utf8_to_wide(label.c_str()));
            start = end + 1;
        }
    }

    blue_run_ui(mb_run, &job);
    return job.result;
}

/* ── Clipboard ────────────────────────────────────────────────────────── */

struct WinClipJob {
    int   write_mode;
    const char* write_text; /* only for write */
    char* read_result;      /* only for read, malloc-owned */
};

static void clip_run(void* p) {
    WinClipJob* job = (WinClipJob*)p;
    if (job->write_mode) {
        std::wstring ws = utf8_to_wide(job->write_text);
        size_t bytes = (ws.size() + 1) * sizeof(wchar_t);
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!hg) return;
        void* ptr = GlobalLock(hg);
        if (!ptr) { GlobalFree(hg); return; }
        memcpy(ptr, ws.c_str(), bytes);
        GlobalUnlock(hg);
        if (OpenClipboard(g_blue_host_hwnd)) {
            EmptyClipboard();
            SetClipboardData(CF_UNICODETEXT, hg);
            CloseClipboard();
        } else {
            GlobalFree(hg);
        }
    } else {
        job->read_result = strdup("");
        if (OpenClipboard(g_blue_host_hwnd)) {
            HGLOBAL hg = GetClipboardData(CF_UNICODETEXT);
            if (hg) {
                wchar_t* wp = (wchar_t*)GlobalLock(hg);
                if (wp) {
                    free(job->read_result);
                    job->read_result = wide_to_utf8_malloc(wp);
                    GlobalUnlock(hg);
                }
            }
            CloseClipboard();
        }
    }
}

extern "C" void jsc_blue_clipboard_write_text(const char* text_utf8) {
    WinClipJob job { 1, text_utf8, nullptr };
    blue_run_ui(clip_run, &job);
}

extern "C" char* jsc_blue_clipboard_read_text(void) {
    WinClipJob job { 0, nullptr, nullptr };
    blue_run_ui(clip_run, &job);
    return job.read_result ? job.read_result : strdup("");
}

#endif /* _WIN32 */
