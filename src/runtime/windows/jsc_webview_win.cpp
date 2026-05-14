/*
 * Windows WebView host.
 *
 * When BLUE_HAVE_WEBVIEW2 is defined (set by main.cpp when WEBVIEW2_SDK_PATH is
 * found), this file creates a real Win32 window hosting the WebView2 control and
 * registers the blue:// scheme for embedded-asset and bridge traffic - matching
 * the Linux GTK/WebKit2 implementation exactly.
 *
 * Without BLUE_HAVE_WEBVIEW2 the original MessageBox fallback is retained and a
 * stderr message tells the user how to enable the full WebView2 path.
 *
 * WEBVIEW2_SDK_PATH should point to the Microsoft.Web.WebView2 NuGet package root
 * (the directory that contains build/native/include/WebView2.h).
 */

#include "../jsc_webview.h"
#include "../jsc_embed.h"
#include "../jsc_runtime.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>

/* WM_APP+1 is handled by our WndProc to run a function on the UI thread.
 * lParam points to an BlueWinDispatch struct defined in jsc_blue_native_win.cpp.
 * Defined here so the WndProc can handle it; the struct is defined in native_win. */
#define WM_BLUE_DISPATCH (WM_APP + 1)

struct BlueWinDispatch {
    void (*fn)(void*);
    void* arg;
};

/* Exposed to jsc_blue_native_win.cpp via extern. */
HWND  g_blue_host_hwnd    = NULL;
DWORD g_blue_ui_thread_id = 0;

static inline std::wstring utf8_to_wide_local(const char* s) {
    if (!s || !s[0]) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

/* ───────────────────────── WebView2 path ─────────────────────────────── */
#ifdef BLUE_HAVE_WEBVIEW2

#include <WebView2.h>

static ICoreWebView2Environment* g_wv2_env    = nullptr;
static ICoreWebView2*            g_webview    = nullptr;
static ICoreWebView2Controller*  g_controller = nullptr;

static LRESULT CALLBACK BlueWndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_BLUE_DISPATCH: {
        auto* d = reinterpret_cast<BlueWinDispatch*>(lp);
        if (d && d->fn) d->fn(d->arg);
        return 0;
    }
    case WM_SIZE:
        if (g_controller) {
            RECT r;
            GetClientRect(hw, &r);
            g_controller->put_Bounds(r);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

/* Converts a wchar_t URL path segment back to UTF-8 and handles the bridge
 * and embedded-asset cases, exactly as jsc_webview_linux.cpp does. */
static std::string wide_to_utf8(const wchar_t* w) {
    if (!w || !w[0]) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

static IStream* stream_from_bytes(const unsigned char* data, size_t len) {
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, len ? len : 1);
    if (!h) return nullptr;
    void* p = GlobalLock(h);
    if (!p) {
        GlobalFree(h);
        return nullptr;
    }
    if (len && data) memcpy(p, data, len);
    GlobalUnlock(h);
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(h, TRUE, &stream))) {
        GlobalFree(h);
        return nullptr;
    }
    return stream;
}

template <typename Interface>
static const IID& webview2_iid();

template <>
const IID& webview2_iid<ICoreWebView2WebResourceRequestedEventHandler>() {
    return IID_ICoreWebView2WebResourceRequestedEventHandler;
}
template <>
const IID& webview2_iid<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>() {
    return IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler;
}
template <>
const IID& webview2_iid<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>() {
    return IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler;
}
template <>
const IID& webview2_iid<ICoreWebView2WebMessageReceivedEventHandler>() {
    return IID_ICoreWebView2WebMessageReceivedEventHandler;
}

template <typename Interface>
struct ComHandlerBase : Interface {
    ULONG refs = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == webview2_iid<Interface>()) {
            *ppv = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&refs);
        if (!r) delete this;
        return r;
    }
};

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::string uri_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = hexval(s[i + 1]);
            int lo = hexval(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back((char)((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                char buf[7];
                snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                out += buf;
            } else {
                out.push_back((char)c);
            }
        }
    }
    return out;
}

struct BlueWebResponse {
    int status = 404;
    std::string mime = "text/plain; charset=utf-8";
    std::string body;
};

static BlueWebResponse handle_blue_url(const std::string& uri) {
    BlueWebResponse out;
    static const std::string pfx = "blue://app/";
    std::string path = uri.size() > pfx.size() ? uri.substr(pfx.size()) : "";

    static const std::string bridge_pfx = "__bridge__/";
    if (path.rfind(bridge_pfx, 0) == 0) {
        std::string rest = path.substr(bridge_pfx.size());
        auto slash = rest.find('/');
        std::string fn  = slash != std::string::npos ? rest.substr(0, slash) : rest;
        std::string arg = slash != std::string::npos ? uri_unescape(rest.substr(slash + 1)) : "";

        char* result = jsc_hybrid_call_aot_cstr(fn.c_str(), arg.c_str());
        out.status = 200;
        out.mime = "text/plain; charset=utf-8";
        out.body = result ? result : "";
        free(result);
        return out;
    }

    const unsigned char* data = nullptr;
    size_t data_len = 0;
    const char* mime = nullptr;
    if (jsc_embed_lookup(path.c_str(), &data, &data_len, &mime)) {
        out.status = 200;
        out.mime = mime && mime[0] ? mime : "application/octet-stream";
        out.body.assign((const char*)data, data_len);
    }
    return out;
}

static std::wstring blue_fetch_shim_script() {
    const char* js =
        "(function(){"
        "if(window.__blueFetchShimInstalled)return;"
        "if(!window.chrome||!chrome.webview)return;"
        "window.__blueFetchShimInstalled=true;"
        "var realFetch=window.fetch?window.fetch.bind(window):null;"
        "var nextId=1,pending={};"
        "chrome.webview.addEventListener('message',function(ev){"
        "var d=ev.data||{};if(d.type!=='blue-fetch-response')return;"
        "var p=pending[d.id];if(!p)return;delete pending[d.id];"
        "if(d.ok){p.resolve(new Response(d.body||'',{status:d.status||200,statusText:'OK',"
        "headers:{'Content-Type':d.mime||'text/plain; charset=utf-8'}}));}"
        "else{p.reject(new TypeError(d.error||'blue fetch failed'));}"
        "});"
        "window.fetch=function(input,init){"
        "var url=(typeof input==='string')?input:((input&&input.url)||'');"
        "if(url.indexOf('blue://app/')===0){"
        "return new Promise(function(resolve,reject){"
        "var id=String(nextId++);pending[id]={resolve:resolve,reject:reject};"
        "chrome.webview.postMessage('blue-fetch\\t'+id+'\\t'+url);"
        "});"
        "}"
        "if(realFetch)return realFetch(input,init);"
        "return Promise.reject(new TypeError('fetch is not available'));"
        "};"
        "})();";
    return utf8_to_wide_local(js);
}

struct WebMessageReceivedHandler
    : ComHandlerBase<ICoreWebView2WebMessageReceivedEventHandler> {
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                     ICoreWebView2WebMessageReceivedEventArgs* args) override {
        LPWSTR msgW = nullptr;
        if (FAILED(args->TryGetWebMessageAsString(&msgW)) || !msgW)
            return S_OK;
        std::string msg = wide_to_utf8(msgW);
        CoTaskMemFree(msgW);

        static const std::string pfx = "blue-fetch\t";
        if (msg.rfind(pfx, 0) != 0)
            return S_OK;
        size_t id_start = pfx.size();
        size_t tab = msg.find('\t', id_start);
        if (tab == std::string::npos)
            return S_OK;
        std::string id = msg.substr(id_start, tab - id_start);
        std::string url = msg.substr(tab + 1);
        BlueWebResponse res = handle_blue_url(url);

        bool ok = res.status >= 200 && res.status < 400;
        std::string json = std::string("{\"type\":\"blue-fetch-response\",\"id\":\"") +
            json_escape(id) + "\",\"ok\":" + (ok ? "true" : "false") +
            ",\"status\":" + std::to_string(res.status) +
            ",\"mime\":\"" + json_escape(res.mime) + "\"" +
            ",\"body\":\"" + json_escape(res.body) + "\"}";
        ICoreWebView2* target = sender ? sender : g_webview;
        if (target) {
            std::wstring wjson = utf8_to_wide_local(json.c_str());
            target->PostWebMessageAsJson(wjson.c_str());
        }
        return S_OK;
    }
};

static HRESULT handle_web_resource_request(ICoreWebView2* wv,
                                           ICoreWebView2WebResourceRequestedEventArgs* args) {
    ICoreWebView2WebResourceRequest* req = nullptr;
    if (FAILED(args->get_Request(&req)) || !req)
        return S_OK;

    LPWSTR uriW = nullptr;
    req->get_Uri(&uriW);
    req->Release();
    std::string uri = wide_to_utf8(uriW);
    CoTaskMemFree(uriW);

    BlueWebResponse web = handle_blue_url(uri);

    if (uri.rfind("blue://app/__bridge__/", 0) == 0) {
        std::string escaped;
        for (char c : web.body) {
            if (c == '\'') escaped += "\\'";
            else if (c == '\\') escaped += "\\\\";
            else if (c == '\n') escaped += "\\n";
            else escaped += c;
        }
        std::wstring js = utf8_to_wide_local(
            ("window.dispatchEvent(new CustomEvent('blue-backend',{detail:'" + escaped + "'}))").c_str());
        wv->ExecuteScript(js.c_str(), nullptr);

        if (g_wv2_env) {
            IStream* empty = stream_from_bytes(nullptr, 0);
            if (empty) {
                ICoreWebView2WebResourceResponse* resp = nullptr;
                g_wv2_env->CreateWebResourceResponse(empty, web.status, L"OK",
                    utf8_to_wide_local(("Content-Type: " + web.mime).c_str()).c_str(), &resp);
                if (resp) {
                    args->put_Response(resp);
                    resp->Release();
                }
                empty->Release();
            }
        }
        return S_OK;
    }

    if (web.status != 200) {
        args->put_Response(nullptr);
        return S_OK;
    }

    if (g_wv2_env) {
        IStream* body = stream_from_bytes((const unsigned char*)web.body.data(), web.body.size());
        if (!body) {
            args->put_Response(nullptr);
            return S_OK;
        }
        std::wstring wmime = utf8_to_wide_local(web.mime.c_str());
        ICoreWebView2WebResourceResponse* resp = nullptr;
        g_wv2_env->CreateWebResourceResponse(
            body, web.status, L"OK", (L"Content-Type: " + wmime).c_str(), &resp);
        if (resp) {
            args->put_Response(resp);
            resp->Release();
        }
        body->Release();
    }
    return S_OK;
}

struct WebResourceRequestedHandler
    : ComHandlerBase<ICoreWebView2WebResourceRequestedEventHandler> {
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender,
                                     ICoreWebView2WebResourceRequestedEventArgs* args) override {
        return handle_web_resource_request(sender, args);
    }
};

struct ControllerCompletedHandler
    : ComHandlerBase<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler> {
    HWND hw;
    std::string html;
    bool* ready;
    ControllerCompletedHandler(HWND h, const std::string& s, bool* r)
        : hw(h), html(s), ready(r) {}
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT, ICoreWebView2Controller* ctrl) override {
        if (!ctrl) return E_FAIL;
        g_controller = ctrl;
        g_controller->AddRef();
        ctrl->get_CoreWebView2(&g_webview);

        RECT r;
        GetClientRect(hw, &r);
        ctrl->put_Bounds(r);

        g_webview->AddWebResourceRequestedFilter(
            L"blue://*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);

        EventRegistrationToken tok = {};
        auto* handler = new WebResourceRequestedHandler();
        g_webview->add_WebResourceRequested(handler, &tok);
        handler->Release();

        EventRegistrationToken msg_tok = {};
        auto* msg_handler = new WebMessageReceivedHandler();
        g_webview->add_WebMessageReceived(msg_handler, &msg_tok);
        msg_handler->Release();

        std::wstring shim = blue_fetch_shim_script();
        g_webview->AddScriptToExecuteOnDocumentCreated(shim.c_str(), nullptr);

        std::wstring whtml = utf8_to_wide_local(html.c_str());
        g_webview->NavigateToString(whtml.c_str());
        if (ready) *ready = true;
        return S_OK;
    }
};

struct EnvironmentCompletedHandler
    : ComHandlerBase<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler> {
    HWND hw;
    std::string html;
    bool* ready;
    EnvironmentCompletedHandler(HWND h, const std::string& s, bool* r)
        : hw(h), html(s), ready(r) {}
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT, ICoreWebView2Environment* env) override {
        if (!env) return E_FAIL;
        g_wv2_env = env;
        g_wv2_env->AddRef();
        auto* handler = new ControllerCompletedHandler(hw, html, ready);
        HRESULT hr = env->CreateCoreWebView2Controller(hw, handler);
        handler->Release();
        return hr;
    }
};

extern "C" void jsc_webview_run_modal(const char* title, const char* html,
                                       int width, int height) {
    g_blue_ui_thread_id = GetCurrentThreadId();
    HRESULT coinit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    /* Register window class */
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = BlueWndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"BlueWebViewHost";
        RegisterClassExW(&wc);
        registered = true;
    }

    std::wstring wtitle = utf8_to_wide_local(title && title[0] ? title : "blue app");

    HWND hw = CreateWindowExW(
        0, L"BlueWebViewHost", wtitle.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width > 0 ? width : 1024, height > 0 ? height : 768,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hw) {
        if (SUCCEEDED(coinit) || coinit == S_FALSE)
            CoUninitialize();
        return;
    }

    g_blue_host_hwnd = hw;
    ShowWindow(hw, SW_SHOW);
    UpdateWindow(hw);

    /* Capture html into a std::string for use in the lambda. */
    std::string html_str = html ? html : "";

    bool wv_ready = false;
    auto* env_handler = new EnvironmentCompletedHandler(hw, html_str, &wv_ready);
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr, env_handler);
    env_handler->Release();

    if (FAILED(hr)) {
        fprintf(stderr, "blue: WebView2 environment creation failed (hr=0x%lx).\n"
                        "       Ensure the Edge WebView2 Runtime is installed.\n", (long)hr);
        g_blue_host_hwnd = NULL;
        DestroyWindow(hw);
        if (SUCCEEDED(coinit) || coinit == S_FALSE)
            CoUninitialize();
        return;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_blue_host_hwnd = NULL;
    if (g_webview)    { g_webview->Release();    g_webview    = nullptr; }
    if (g_controller) { g_controller->Release(); g_controller = nullptr; }
    if (g_wv2_env)    { g_wv2_env->Release();    g_wv2_env    = nullptr; }
    if (SUCCEEDED(coinit) || coinit == S_FALSE)
        CoUninitialize();
}

/* ───────────────────────── Fallback (no WebView2 SDK) ───────────────── */
#else  /* BLUE_HAVE_WEBVIEW2 not defined */

static LRESULT CALLBACK BlueWndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_BLUE_DISPATCH: {
        auto* d = reinterpret_cast<BlueWinDispatch*>(lp);
        if (d && d->fn) d->fn(d->arg);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

extern "C" void jsc_webview_run_modal(const char* title, const char* html,
                                       int width, int height) {
    (void)width; (void)height;
    (void)jsc_embed_lookup;
    fprintf(stderr,
        "blue: WebView2 SDK not found. Set WEBVIEW2_SDK_PATH to the\n"
        "       Microsoft.Web.WebView2 NuGet package root to enable the\n"
        "       full WebView2 window. Falling back to MessageBox preview.\n");

    /* Create a minimal invisible window so window/dialog/clipboard APIs work. */
    g_blue_ui_thread_id = GetCurrentThreadId();
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = BlueWndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"BlueWebViewHost";
        RegisterClassExW(&wc);
        registered = true;
    }
    HWND hw = CreateWindowExW(0, L"BlueWebViewHost",
        L"blue", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1, 1,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    g_blue_host_hwnd = hw;

    std::wstring wtitle = utf8_to_wide_local(title && title[0] ? title : "blue app");
    std::wstring whtml  = utf8_to_wide_local(html ? html : "");
    MessageBoxW(hw, whtml.c_str(), wtitle.c_str(), MB_OK | MB_ICONINFORMATION);

    g_blue_host_hwnd = NULL;
    if (hw) DestroyWindow(hw);
}

#endif /* BLUE_HAVE_WEBVIEW2 */
#else  /* !_WIN32 */
extern "C" void jsc_webview_run_modal(const char*, const char*, int, int) {}
#endif /* _WIN32 */
