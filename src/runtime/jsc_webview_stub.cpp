#include "jsc_webview.h"
#include <stdio.h>

extern "C" void jsc_webview_run_modal(const char* title, const char* html,
                                       int width, int height) {
    (void)title;
    (void)html;
    (void)width;
    (void)height;
    fprintf(stderr,
            "blue: native WebView is not linked (build with ./blue -build or "
            "pass GTK/WebKit or WebKit2 flags for -compile).\n");
}
