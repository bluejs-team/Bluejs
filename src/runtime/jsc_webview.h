/*
 * blue_webview.h - Cross-platform modal WebView (Linux WebKitGTK, macOS WKWebView,
 * Windows WebView2 when available).
 */

#ifndef JSC_WEBVIEW_H
#define JSC_WEBVIEW_H

#ifdef __cplusplus
extern "C" {
#endif

void jsc_webview_run_modal(const char* title, const char* html, int width,
                           int height);

#ifdef __cplusplus
}
#endif

#endif /* JSC_WEBVIEW_H */
