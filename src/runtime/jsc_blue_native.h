#pragma once

/*
 * C ABI for Blue.* desktop/native operations (GTK WebView host window on Linux).
 * Implemented by either jsc_blue_native_linux.cpp or jsc_blue_native_stub.cpp.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BlueExecResult BlueExecResult;

struct BlueExecResult {
    int    exit_code;
    char*  stdout_txt; /* malloc-owned; may be empty */
    char*  stderr_txt; /* malloc-owned; may be empty */
};

void jsc_blue_window_set_title(const char* title_utf8);
void  jsc_blue_window_set_size(int w, int h);
void  jsc_blue_window_center(void);
void  jsc_blue_window_set_frameless(int frameless_boolean);
void  jsc_blue_window_minimize(void);
void  jsc_blue_window_maximize(void);
void  jsc_blue_window_close(void);
void  jsc_blue_window_set_always_on_top(int on_boolean);

/*
 * Blocking native file dialogs. Returns malloc'd NUL-terminated UTF-8:
 * Open: paths joined with '|' delimiter, empty string cancelled.
 * Save: single path or empty cancelled.
 */
char* jsc_blue_dialog_open_files(const char* title_utf8, int multiple,
                                   const char* filter_desc,
                                   const char* filter_patterns_csv);
char* jsc_blue_dialog_save_file(const char* title_utf8, const char* default_name_utf8,
                                  const char* filter_patterns_csv);

/* Buttons: newline-separated UTF-8; returns 0-based index or -1 on cancel/error. */
int jsc_blue_dialog_message_box(const char* title_utf8, const char* message_utf8,
                                  const char* dialog_type_ascii,
                                  const char* buttons_lines_utf8);

void  jsc_blue_clipboard_write_text(const char* text_utf8);
char* jsc_blue_clipboard_read_text(void);

BlueExecResult* jsc_blue_process_exec(const char* command_utf8);
void             jsc_blue_exec_result_free(BlueExecResult* r);

/* JSON-ish plain text lines: "{ ... }" - minimal keys for portability */
char* jsc_blue_system_memory_json(void);
char* jsc_blue_system_cpu_json(void);


#ifdef __cplusplus
}
#endif
