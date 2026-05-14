#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Modal GtkWindow pointer while jsc_webview_run_modal GTK host is alive.
 */
void* jsc_blue_host_gtk_window_ptr(void);

#ifdef __cplusplus
}
#endif
