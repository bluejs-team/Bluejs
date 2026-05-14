/*
 * QuickJS bindings for Blue.* desktop API (hybrid / control-plane islands).
 */

#if (defined(JSC_QJS_CONTROL_PLANE) || defined(JSC_HYBRID)) &&                  \
    defined(JSC_RUNTIME_NODE_IO) && defined(JSC_HAVE_UV)

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <quickjs.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "jsc_blue_native.h"
#include "jsc_blue_plugin.h"

#include <cstdlib>
#include <cstring>

static char* qa_strdup_js(JSContext* ctx, JSValueConst v, const char* fb) {
    const char* c = JS_ToCString(ctx, v);
    char*       o = strdup(c ? c : (fb ? fb : ""));
    if (c)
        JS_FreeCString(ctx, c);
    return o;
}

static JSValue qa_win_title(JSContext* ctx, JSValueConst, int argc,
                             JSValueConst* argv) {
    char* s = qa_strdup_js(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, "");
    jsc_blue_window_set_title(s);
    free(s);
    return JS_UNDEFINED;
}
static JSValue qa_win_size(JSContext* ctx, JSValueConst, int argc,
                            JSValueConst* argv) {
    int32_t w = 0, h = 0;
    if (argc > 0)
        (void)JS_ToInt32(ctx, &w, argv[0]);
    if (argc > 1)
        (void)JS_ToInt32(ctx, &h, argv[1]);
    jsc_blue_window_set_size((int)w, (int)h);
    return JS_UNDEFINED;
}
static JSValue qa_win_center(JSContext*, JSValueConst, int, JSValueConst*) {
    jsc_blue_window_center();
    return JS_UNDEFINED;
}
static JSValue qa_win_frameless(JSContext* ctx, JSValueConst, int argc,
                                 JSValueConst* argv) {
    int b = argc > 0 ? JS_ToBool(ctx, argv[0]) : 0;
    jsc_blue_window_set_frameless(b);
    return JS_UNDEFINED;
}
static JSValue qa_win_min(JSContext*, JSValueConst, int, JSValueConst*) {
    jsc_blue_window_minimize();
    return JS_UNDEFINED;
}
static JSValue qa_win_max(JSContext*, JSValueConst, int, JSValueConst*) {
    jsc_blue_window_maximize();
    return JS_UNDEFINED;
}
static JSValue qa_win_close(JSContext*, JSValueConst, int, JSValueConst*) {
    jsc_blue_window_close();
    return JS_UNDEFINED;
}
static JSValue qa_win_ontop(JSContext* ctx, JSValueConst, int argc,
                             JSValueConst* argv) {
    int b = argc > 0 ? JS_ToBool(ctx, argv[0]) : 0;
    jsc_blue_window_set_always_on_top(b);
    return JS_UNDEFINED;
}

static JSValue qa_dlg_open(JSContext* ctx, JSValueConst, int argc,
                           JSValueConst* argv) {
    char* title =
        qa_strdup_js(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, "Open");
    char* pat =
        qa_strdup_js(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, "");
    int   mul =
        argc > 2 ? (JS_ToBool(ctx, argv[2]) ? 1 : 0) : 0;
    char* paths = jsc_blue_dialog_open_files(title, mul, "", pat);
    free(pat);
    free(title);
    JSValue rv = JS_NewString(ctx, paths ? paths : "");
    free(paths);
    return rv;
}

static JSValue qa_dlg_save(JSContext* ctx, JSValueConst, int argc,
                           JSValueConst* argv) {
    char* title =
        qa_strdup_js(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, "Save");
    char* def =
        qa_strdup_js(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, "");
    char* p = jsc_blue_dialog_save_file(title, def, "");
    free(def);
    free(title);
    JSValue rv = JS_NewString(ctx, p ? p : "");
    free(p);
    return rv;
}

static JSValue qa_dlg_msg(JSContext* ctx, JSValueConst, int argc,
                          JSValueConst* argv) {
    char *title, *msg, *typ, *btn;
    title = qa_strdup_js(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, "Blue");
    msg   = qa_strdup_js(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, "");
    typ   = qa_strdup_js(ctx, argc > 2 ? argv[2] : JS_UNDEFINED, "info");
    btn =
        qa_strdup_js(ctx, argc > 3 ? argv[3] : JS_UNDEFINED, "OK");
    int idx = jsc_blue_dialog_message_box(title, msg, typ, btn);
    free(btn);
    free(typ);
    free(msg);
    free(title);
    return JS_NewInt32(ctx, idx);
}

static JSValue qa_clip_write(JSContext* ctx, JSValueConst, int argc,
                              JSValueConst* argv) {
    char* s = qa_strdup_js(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, "");
    jsc_blue_clipboard_write_text(s);
    free(s);
    return JS_UNDEFINED;
}
static JSValue qa_clip_read(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    char* t = jsc_blue_clipboard_read_text();
    JSValue rv = JS_NewString(ctx, t ? t : "");
    free(t);
    return rv;
}

static JSValue qa_proc_exec(JSContext* ctx, JSValueConst, int argc,
                             JSValueConst* argv) {
    char* cmd = qa_strdup_js(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, "");
    BlueExecResult* r = jsc_blue_process_exec(cmd);
    free(cmd);
    JSValue o = JS_NewObject(ctx);
    if (!r) {
        JS_SetPropertyStr(ctx, o, "code", JS_NewInt32(ctx, -1));
        JS_SetPropertyStr(ctx, o, "stdout", JS_NewString(ctx, ""));
        JS_SetPropertyStr(ctx, o, "stderr", JS_NewString(ctx, ""));
        return o;
    }
    JS_SetPropertyStr(ctx, o, "code", JS_NewInt32(ctx, r->exit_code));
    JS_SetPropertyStr(ctx, o, "stdout",
                      JS_NewString(ctx, r->stdout_txt ? r->stdout_txt : ""));
    JS_SetPropertyStr(ctx, o, "stderr",
                      JS_NewString(ctx, r->stderr_txt ? r->stderr_txt : ""));
    jsc_blue_exec_result_free(r);
    return o;
}

static JSValue qa_sys_mem(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    char* j = jsc_blue_system_memory_json();
    JSValue v = JS_NewString(ctx, j ? j : "{}");
    free(j);
    return v;
}
static JSValue qa_sys_cpu(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    char* j = jsc_blue_system_cpu_json();
    JSValue v = JS_NewString(ctx, j ? j : "{}");
    free(j);
    return v;
}


extern "C" void jsc_qjs_install_blue_desktop(JSContext* ctx) {
#define ADD(sub, meth, func, arity)                                            \
    JS_SetPropertyStr(                                                           \
        ctx, sub, meth, JS_NewCFunction(ctx, func, meth, arity))

    JSValue Win = JS_NewObject(ctx);
    ADD(Win, "setTitle", qa_win_title, 1);
    ADD(Win, "setSize", qa_win_size, 2);
    ADD(Win, "center", qa_win_center, 0);
    ADD(Win, "setFrameless", qa_win_frameless, 1);
    ADD(Win, "minimize", qa_win_min, 0);
    ADD(Win, "maximize", qa_win_max, 0);
    ADD(Win, "close", qa_win_close, 0);
    ADD(Win, "setAlwaysOnTop", qa_win_ontop, 1);

    JSValue Dlg = JS_NewObject(ctx);
    ADD(Dlg, "showOpenDialog", qa_dlg_open, 3);
    ADD(Dlg, "showSaveDialog", qa_dlg_save, 2);
    ADD(Dlg, "showMessageBox", qa_dlg_msg, 4);

    JSValue Clip = JS_NewObject(ctx);
    ADD(Clip, "writeText", qa_clip_write, 1);
    ADD(Clip, "readText", qa_clip_read, 0);

    JSValue Proc = JS_NewObject(ctx);
    ADD(Proc, "exec", qa_proc_exec, 1);

    JSValue Sys = JS_NewObject(ctx);
    ADD(Sys, "getMemoryInfo", qa_sys_mem, 0);
    ADD(Sys, "getCPU", qa_sys_cpu, 0);

#undef ADD

    JSValue Blue = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, Blue, "Window", Win);
    JS_SetPropertyStr(ctx, Blue, "Dialog", Dlg);
    JS_SetPropertyStr(ctx, Blue, "Clipboard", Clip);
    JS_SetPropertyStr(ctx, Blue, "Process", Proc);
    JS_SetPropertyStr(ctx, Blue, "System", Sys);
    jsc_blue_plugin_install_js(ctx, &Blue);

    JSValue g = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, g, "Blue", Blue);
    JS_FreeValue(ctx, g);
}

#endif
