#include "../jsc_webview.h"
#include "../jsc_embed.h"
#include "../jsc_runtime.h"

#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

extern "C" {
extern int    jsc_argc;
extern char** jsc_argv;
}

/* Set while the modal WebView is active (scheme callback has no web_view on
 * some WebKit versions). */
static WebKitWebView* jsc_active_webview = nullptr;

/* Blue native Linux uses this while a modal host exists. */
static GtkWindow* g_blue_modal_gtk_host = nullptr;

extern "C" void* jsc_blue_host_gtk_window_ptr(void) {
    return (void*)g_blue_modal_gtk_host;
}

static char* normalize_jsc_uri_path(const gchar* uri) {
    if (!uri) return nullptr;
    const gchar* s = strstr(uri, "://");
    if (!s) return nullptr;
    s += 3;
    /* blue:///x or blue://host/x -> path segment after authority */
    const gchar* slash = strchr(s, '/');
    if (!slash)
        return strdup(s);
    if (slash[1] == '\0')
        return strdup("");
    return strdup(slash + 1);
}

static void free_norm(char* norm) {
    if (norm) free(norm);
}

typedef struct {
    WebKitWebView* wv;
    char*          js;
} JscPendingJs;

static void jsc_eval_js_done(GObject* source, GAsyncResult* res, gpointer user) {
    WebKitWebView* wv = WEBKIT_WEB_VIEW(source);
    GError*        err = nullptr;
    JSCValue*      val =
        webkit_web_view_evaluate_javascript_finish(wv, res, &err);
    if (val)
        g_object_unref(val);
    if (err)
        g_error_free(err);
    JscPendingJs* p = (JscPendingJs*)user;
    g_free(p->js);
    g_free(p);
}

static gboolean jsc_run_js_idle(gpointer user) {
    JscPendingJs* p = (JscPendingJs*)user;
    if (p->wv && p->js)
        webkit_web_view_evaluate_javascript(
            p->wv, p->js, (gssize)-1, nullptr, nullptr, nullptr,
            (GAsyncReadyCallback)jsc_eval_js_done, p);
    else {
        g_free(p->js);
        g_free(p);
    }
    return G_SOURCE_REMOVE;
}

/* Frontend → native: fetch('blue://app/__bridge__/fn/'+encodeURIComponent(payload))
 * Native → frontend: CustomEvent 'blue-backend' with JSON detail. */
static void jsc_bridge_handle(WebKitWebView* wv, const char* encoded_tail) {
    if (!wv || !encoded_tail)
        return;
    char* decoded =
        g_uri_unescape_string(encoded_tail, nullptr);
    if (!decoded)
        decoded = g_strdup("");

    gchar* b64 = g_base64_encode((const guchar*)decoded, (gsize)strlen(decoded));
    g_free(decoded);

    char* js = g_strdup_printf(
        "window.dispatchEvent(new CustomEvent('blue-backend',{detail:{ok:true,"
        "from:'native',payloadB64:'%s'}}));",
        b64);
    g_free(b64);

    JscPendingJs* job = (JscPendingJs*)g_malloc(sizeof(JscPendingJs));
    job->wv = wv;
    job->js = js;
    g_idle_add(jsc_run_js_idle, job);
}

static void jsc_uri_scheme_cb(WebKitURISchemeRequest* request, gpointer /*ud*/) {
    const gchar* uri = webkit_uri_scheme_request_get_uri(request);
    char* norm = normalize_jsc_uri_path(uri);
    if (!norm || norm[0] == '\0') {
        free_norm(norm);
        GError* err = g_error_new(WEBKIT_NETWORK_ERROR_FAILED,
                                   WEBKIT_NETWORK_ERROR_FAILED,
                                   "Invalid blue: URI");
        webkit_uri_scheme_request_finish_error(request, err);
        g_error_free(err);
        return;
    }

    WebKitWebView* wv = jsc_active_webview;

    if (!strncmp(norm, "__bridge__/", 11)) {
        const char* tail = norm + 11;
        const char* slash = strchr(tail, '/');
        const char* fn = tail;
        const char* arg = "";
        if (slash) {
            size_t fn_len = (size_t)(slash - tail);
            char* fn_copy = (char*)malloc(fn_len + 1);
            if (!fn_copy) return;
            memcpy(fn_copy, tail, fn_len);
            fn_copy[fn_len] = '\0';
            fn = fn_copy;
            arg = slash + 1;
        }
        char* decoded_arg = g_uri_unescape_string(arg, nullptr);
        if (!decoded_arg)
            decoded_arg = g_strdup("");
        char* out = jsc_hybrid_call_aot_cstr(fn, decoded_arg);
        if (slash)
            free((void*)fn);
        g_free(decoded_arg);
        const char* body = out ? out : "";
        GInputStream* stm = g_memory_input_stream_new_from_data(
            g_strdup(body), (gssize)strlen(body), (GDestroyNotify)g_free);
        webkit_uri_scheme_request_finish(request, stm, (gssize)strlen(body),
                                         "text/plain; charset=utf-8");
        g_object_unref(stm);
        if (out)
            free(out);
        jsc_bridge_handle(wv, tail);
        free_norm(norm);
        return;
    }

    const unsigned char* data = nullptr;
    size_t len = 0;
    const char* mime = nullptr;
    if (!jsc_embed_lookup(norm, &data, &len, &mime)) {
        GError* err = g_error_new(WEBKIT_NETWORK_ERROR_FAILED,
                                   WEBKIT_NETWORK_ERROR_FAILED,
                                   "Not embedded: %s", norm ? norm : "");
        free_norm(norm);
        webkit_uri_scheme_request_finish_error(request, err);
        g_error_free(err);
        return;
    }
    free_norm(norm);

    const char* ctype = mime && mime[0] ? mime : "application/octet-stream";
    unsigned char* copy = (unsigned char*)malloc(len ? len : 1);
    if (!copy)
        return;
    if (len) memcpy(copy, data, len);
    GInputStream* stm =
        g_memory_input_stream_new_from_data(copy, (gssize)len,
                                            (GDestroyNotify)free);
    webkit_uri_scheme_request_finish(request, stm, (gssize)len, ctype);
    g_object_unref(stm);
}

extern "C" void jsc_webview_run_modal(const char* title, const char* html,
                                       int width, int height) {
    /* b21e226b used gtk_init(0, NULL). When `js_process_init` has run, pass real
     * argc/argv so GTK+ can honor `--display=` etc. */
    int    gtk_argc = 0;
    char** gtk_argv = nullptr;
    if (jsc_argc > 0 && jsc_argv) {
        gtk_argc = jsc_argc;
        gtk_argv = jsc_argv;
    }
    gtk_init(&gtk_argc, &gtk_argv);

    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win),
                         title ? title : "blue app");
    gtk_window_set_default_size(GTK_WINDOW(win), width > 0 ? width : 800,
                                height > 0 ? height : 600);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    WebKitWebView* wv = WEBKIT_WEB_VIEW(webkit_web_view_new());
    WebKitWebContext* ctx = webkit_web_view_get_context(wv);

    webkit_web_context_register_uri_scheme(ctx, "blue", jsc_uri_scheme_cb,
                                           nullptr, nullptr);

    gtk_container_add(GTK_CONTAINER(win), GTK_WIDGET(wv));
    gtk_widget_show_all(win);

    g_blue_modal_gtk_host = GTK_WINDOW(win);
    jsc_active_webview = wv;
    const char* base = "blue://app/";
    webkit_web_view_load_html(wv, html ? html : "", base);
    gtk_main();
    jsc_active_webview     = nullptr;
    g_blue_modal_gtk_host = nullptr;
}
