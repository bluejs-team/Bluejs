/*
 * GTK-native Blue.* window / dialog / clipboard when a modal WebView host exists.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "../jsc_blue_native.h"
#include "jsc_blue_host_linux.h"

#include <glib.h>
#include <gtk/gtk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GtkWindow* host(void) {
    return (GtkWindow*)jsc_blue_host_gtk_window_ptr();
}

typedef struct BlueUiSync {
    void (*wk)(gpointer);
    gpointer arg;
    GMutex   mx;
    GCond    cd;
    gboolean finished;
} BlueUiSync;

static gboolean blue_ui_sync_idle_dispatch(gpointer p) {
    BlueUiSync* s = (BlueUiSync*)p;
    if (s->wk)
        s->wk(s->arg);
    g_mutex_lock(&s->mx);
    s->finished = TRUE;
    g_cond_signal(&s->cd);
    g_mutex_unlock(&s->mx);
    return G_SOURCE_REMOVE;
}

static void blue_run_ui(void (*worker)(gpointer), gpointer ud) {
    /* Run directly when:
     *   (a) GTK main loop is not running yet (before window.open), OR
     *   (b) we ARE the GTK main thread - e.g. called from a WebView bridge
     *       URI-scheme callback.  Posting g_idle_add then blocking on a
     *       g_cond_wait would deadlock because the idle can never fire while
     *       the main thread is waiting. */
    if (gtk_main_level() == 0 ||
        g_main_context_is_owner(g_main_context_default())) {
        if (worker)
            worker(ud);
        return;
    }

    BlueUiSync s;
    memset(&s, 0, sizeof(BlueUiSync));
    s.wk        = worker;
    s.arg       = ud;
    s.finished  = FALSE;
    g_mutex_init(&s.mx);
    g_cond_init(&s.cd);

    g_idle_add(blue_ui_sync_idle_dispatch, &s);

    g_mutex_lock(&s.mx);
    while (!s.finished)
        g_cond_wait(&s.cd, &s.mx);
    g_mutex_unlock(&s.mx);
    g_mutex_clear(&s.mx);
    g_cond_clear(&s.cd);
}

/* --- Window --- */

typedef struct {
    const char* title;
    int         w;
    int         h;
    int         ib_frameless;
    int         ib_always_top;
    int         op;
} WJob;

static void wjob_run(gpointer p) {
    WJob*      j = (WJob*)p;
    GtkWindow* w = host();
    if (!w)
        return;
    switch (j->op) {
    case 0:
        if (j->title && j->title[0])
            gtk_window_set_title(w, j->title);
        break;
    case 1: {
        int ww = j->w > 0 ? j->w : 800;
        int hh = j->h > 0 ? j->h : 600;
        gtk_window_resize(w, ww, hh);
        break;
    }
    case 2:
        gtk_window_set_position(w, GTK_WIN_POS_CENTER);
        break;
    case 3:
        gtk_window_set_decorated(w, j->ib_frameless ? FALSE : TRUE);
        break;
    case 4:
        gtk_window_iconify(w);
        break;
    case 5:
        gtk_window_maximize(w);
        break;
    case 6:
        gtk_widget_destroy(GTK_WIDGET(w));
        break;
    case 7:
        gtk_window_set_keep_above(w, j->ib_always_top ? TRUE : FALSE);
        break;
    default: break;
    }
}

#define WJOB(op_)                                                              \
    do {                                                                       \
        WJob j_;                                                               \
        memset(&j_, 0, sizeof(j_));                                             \
        j_.op = (op_);                                                         \
        blue_run_ui(wjob_run, &j_);                                           \
    } while (0)

extern "C" void jsc_blue_window_set_title(const char* title_utf8) {
    WJob j;
    memset(&j, 0, sizeof(j));
    j.op    = 0;
    j.title = title_utf8;
    blue_run_ui(wjob_run, &j);
}
extern "C" void jsc_blue_window_set_size(int w, int h) {
    WJob j;
    memset(&j, 0, sizeof(j));
    j.op = 1;
    j.w  = w;
    j.h  = h;
    blue_run_ui(wjob_run, &j);
}
extern "C" void jsc_blue_window_center(void) { WJOB(2); }
extern "C" void jsc_blue_window_set_frameless(int frameless_boolean) {
    WJob j;
    memset(&j, 0, sizeof(j));
    j.op             = 3;
    j.ib_frameless   = frameless_boolean;
    blue_run_ui(wjob_run, &j);
}
extern "C" void jsc_blue_window_minimize(void) { WJOB(4); }
extern "C" void jsc_blue_window_maximize(void) { WJOB(5); }
extern "C" void jsc_blue_window_close(void) { WJOB(6); }
extern "C" void jsc_blue_window_set_always_on_top(int on_boolean) {
    WJob j;
    memset(&j, 0, sizeof(j));
    j.op              = 7;
    j.ib_always_top   = on_boolean;
    blue_run_ui(wjob_run, &j);
}

/* --- Open dialog --- */

typedef struct {
    const char* title;
    int         multi;
    const char* filt_desc;
    const char* patterns_csv;
    char*       out;
} ODJob;

static void od_run(gpointer p) {
    ODJob*     job = (ODJob*)p;
    GtkWindow* w   = host();
    job->out       = strdup("");
    if (!w)
        return;
    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        job->title && job->title[0] ? job->title : "Open file", w,
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dlg),
                                         job->multi ? TRUE : FALSE);

    if (job->patterns_csv && job->patterns_csv[0]) {
        GtkFileFilter* filt = gtk_file_filter_new();
        gtk_file_filter_set_name(
            filt, (job->filt_desc && job->filt_desc[0]) ? job->filt_desc : "Filtered");
        char* dup = strdup(job->patterns_csv);
        char* sp  = dup;
        char* tok = nullptr;
        while ((tok = strsep(&sp, ",;")) != nullptr && *tok) {
            while (*tok == ' ')
                ++tok;
            char pat[128];
            if (strchr(tok, '*'))
                snprintf(pat, sizeof pat, "%s", tok);
            else
                snprintf(pat, sizeof pat, "*.%s", tok);
            gtk_file_filter_add_pattern(filt, pat);
        }
        free(dup);
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), filt);
    }

    gint res = gtk_dialog_run(GTK_DIALOG(dlg));
    free(job->out);
    job->out = strdup("");

    if (res == GTK_RESPONSE_ACCEPT) {
        GString* acc = g_string_new("");
        if (job->multi) {
            GSList* list =
                gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dlg));
            for (GSList* it = list; it; it = it->next) {
                if (acc->len)
                    g_string_append_c(acc, '|');
                g_string_append(acc, (const char*)it->data);
                g_free(it->data);
            }
            g_slist_free(list);
        } else {
            gchar* fp =
                gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
            if (fp) {
                g_string_append(acc, fp);
                g_free(fp);
            }
        }
        free(job->out);
        job->out = strdup(acc->len ? acc->str : "");
        g_string_free(acc, TRUE);
    }

    gtk_widget_destroy(dlg);
}

extern "C" char* jsc_blue_dialog_open_files(const char* title_utf8, int multiple,
                                              const char* filter_desc,
                                              const char* filter_patterns_csv) {
    ODJob j;
    memset(&j, 0, sizeof(j));
    j.title        = title_utf8;
    j.multi        = multiple;
    j.filt_desc    = filter_desc;
    j.patterns_csv = filter_patterns_csv;
    j.out          = strdup("");
    blue_run_ui(od_run, &j);
    return j.out ? j.out : strdup("");
}

/* --- Save dialog --- */

typedef struct {
    const char* title;
    const char* defname;
    char*       out;
} SDJob;

static void sd_run(gpointer p) {
    SDJob* job = (SDJob*)p;
    GtkWindow* w = host();
    job->out     = strdup("");
    if (!w)
        return;
    GtkWidget* dlg =
        gtk_file_chooser_dialog_new(
            job->title && job->title[0] ? job->title : "Save file", w,
            GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL,
            "_Save", GTK_RESPONSE_ACCEPT, NULL);
    if (job->defname && job->defname[0])
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg),
                                          job->defname);

    gint res = gtk_dialog_run(GTK_DIALOG(dlg));

    free(job->out);
    job->out = strdup("");

    if (res == GTK_RESPONSE_ACCEPT) {
        gchar* fn =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (fn) {
            free(job->out);
            job->out = strdup(fn);
            g_free(fn);
        }
    }
    gtk_widget_destroy(dlg);
}

extern "C" char* jsc_blue_dialog_save_file(const char* title_utf8,
                                             const char* default_name_utf8,
                                             const char* /*filter_patterns_csv*/) {
    SDJob j;
    memset(&j, 0, sizeof(j));
    j.title   = title_utf8;
    j.defname = default_name_utf8;
    j.out     = strdup("");
    blue_run_ui(sd_run, &j);
    return j.out ? j.out : strdup("");
}

/* --- Message box --- */

typedef struct {
    const char* title;
    const char* msg;
    const char* typ;
    const char* btns_lines;
    int         out_idx;
} MBJob;

static void mb_run(gpointer p) {
    MBJob* job     = (MBJob*)p;
    GtkWindow* w   = host();
    job->out_idx   = -1;
    if (!w)
        return;

    GtkMessageType mt = GTK_MESSAGE_OTHER;
    if (job->typ) {
        if (!strcmp(job->typ, "error"))
            mt = GTK_MESSAGE_ERROR;
        else if (!strcmp(job->typ, "warning"))
            mt = GTK_MESSAGE_WARNING;
        else if (!strcmp(job->typ, "info"))
            mt = GTK_MESSAGE_INFO;
        else if (!strcmp(job->typ, "question"))
            mt = GTK_MESSAGE_QUESTION;
    }

    GtkWidget* dlg = gtk_message_dialog_new(
        w,
        (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), mt,
        GTK_BUTTONS_NONE, "%s", job->msg ? job->msg : "");
    gtk_window_set_title(GTK_WINDOW(dlg), job->title ? job->title : "Blue");

    if (job->btns_lines && job->btns_lines[0]) {
        char* copy = strdup(job->btns_lines);
        int   idx  = 0;
        char* line = nullptr;
        char* brk  = nullptr;
        for (line = strtok_r(copy, "\n", &brk); line;
             line = strtok_r(nullptr, "\n", &brk)) {
            while (*line == ' ' || *line == '\t')
                ++line;
            if (!*line)
                continue;
            gtk_dialog_add_button(GTK_DIALOG(dlg), line, 100 + idx);
            ++idx;
        }
        free(copy);
    } else
        gtk_dialog_add_button(GTK_DIALOG(dlg), "_OK", GTK_RESPONSE_OK);

    gint res = gtk_dialog_run(GTK_DIALOG(dlg));
    if (res >= 100)
        job->out_idx = (int)(res - 100);
    else if (res == GTK_RESPONSE_OK)
        job->out_idx = 0;
    else
        job->out_idx = -1;

    gtk_widget_destroy(dlg);
}

extern "C" int jsc_blue_dialog_message_box(const char* title_utf8,
                                            const char* message_utf8,
                                            const char* dialog_type_ascii,
                                            const char* buttons_lines_utf8) {
    MBJob j;
    memset(&j, 0, sizeof(j));
    j.title      = title_utf8;
    j.msg        = message_utf8;
    j.typ        = dialog_type_ascii;
    j.btns_lines = buttons_lines_utf8;
    j.out_idx    = -1;
    blue_run_ui(mb_run, &j);
    return j.out_idx;
}

/* --- Clipboard --- */

typedef struct {
    const char* text;
    char*       read_out;
    int         write_mode;
} CJob;

static void clip_run(gpointer p) {
    CJob* job = (CJob*)p;
    GdkDisplay* disp = gdk_display_get_default();
    if (!disp)
        return;
    GtkClipboard* cb =
        gtk_clipboard_get_default(disp);
    if (!cb)
        return;
    if (job->write_mode) {
        if (job->text)
            gtk_clipboard_set_text(cb, job->text, -1);
    } else {
        gchar* t = gtk_clipboard_wait_for_text(cb);
        free(job->read_out);
        job->read_out = t ? strdup(t) : strdup("");
        if (t)
            g_free(t);
    }
}

extern "C" void jsc_blue_clipboard_write_text(const char* text_utf8) {
    CJob j;
    memset(&j, 0, sizeof(j));
    j.text       = text_utf8;
    j.write_mode = 1;
    blue_run_ui(clip_run, &j);
}

extern "C" char* jsc_blue_clipboard_read_text(void) {
    CJob j;
    memset(&j, 0, sizeof(j));
    j.write_mode = 0;
    j.read_out   = strdup("");
    blue_run_ui(clip_run, &j);
    return j.read_out ? j.read_out : strdup("");
}

#undef WJOB
