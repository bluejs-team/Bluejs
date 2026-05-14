/*
 * js_window.h - Native modal WebView invoked from compiled JS (`window.open`).
 *
 * Implemented by linking one of src/runtime/jsc_webview_{linux,mac,win,stub}.cpp;
 * see `blue -compile` / `blue -build`, which attach the platform TU automatically.
 */

#ifndef JS_WINDOW_H
#define JS_WINDOW_H

#include "runtime/jsc_webview.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void window_open(const char* title,
                                const char* html_content,
                                int width, int height) {
    jsc_webview_run_modal(title ? title : "blue app",
                          html_content ? html_content : "", width, height);
}

#ifdef __cplusplus
}
#endif

#endif /* JS_WINDOW_H */
