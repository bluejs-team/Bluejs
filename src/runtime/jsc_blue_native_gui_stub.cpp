/*
 * Fallback Window / Dialog / Clipboard when Linux GTK Blue GUI is not linked.
 */

#include "jsc_blue_native.h"

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void warn(const char* what) {
    fprintf(stderr,
            "blue: Blue native GUI: %s (link GTK WebKit build for modal host)\n",
            what);
}

extern "C" void jsc_blue_window_set_title(const char* t) {
    (void)t;
    warn("Window.setTitle");
}
extern "C" void jsc_blue_window_set_size(int w, int h) {
    (void)w;
    (void)h;
    warn("Window.setSize");
}
extern "C" void jsc_blue_window_center(void) { warn("Window.center"); }
extern "C" void jsc_blue_window_set_frameless(int fb) {
    (void)fb;
    warn("Window.setFrameless");
}
extern "C" void jsc_blue_window_minimize(void) { warn("Window.minimize"); }
extern "C" void jsc_blue_window_maximize(void) { warn("Window.maximize"); }
extern "C" void jsc_blue_window_close(void) { warn("Window.close"); }
extern "C" void jsc_blue_window_set_always_on_top(int on) {
    (void)on;
    warn("Window.setAlwaysOnTop");
}

extern "C" char* jsc_blue_dialog_open_files(const char* title_utf8,
                                              int multiple,
                                              const char* filter_desc,
                                              const char* filter_patterns_csv) {
    (void)title_utf8;
    (void)multiple;
    (void)filter_desc;
    (void)filter_patterns_csv;
    warn("Dialog.showOpenDialog");
    return strdup("");
}
extern "C" char* jsc_blue_dialog_save_file(const char* title_utf8,
                                             const char* default_name_utf8,
                                             const char* filter_patterns_csv) {
    (void)title_utf8;
    (void)default_name_utf8;
    (void)filter_patterns_csv;
    warn("Dialog.showSaveDialog");
    return strdup("");
}
extern "C" int jsc_blue_dialog_message_box(const char* title_utf8,
                                            const char* message_utf8,
                                            const char* dialog_type_ascii,
                                            const char* buttons_lines_utf8) {
    (void)title_utf8;
    (void)message_utf8;
    (void)dialog_type_ascii;
    (void)buttons_lines_utf8;
    warn("Dialog.showMessageBox");
    return -1;
}

extern "C" void jsc_blue_clipboard_write_text(const char* text_utf8) {
    (void)text_utf8;
    warn("Clipboard.writeText");
}

extern "C" char* jsc_blue_clipboard_read_text(void) {
    warn("Clipboard.readText");
    return strdup("");
}
