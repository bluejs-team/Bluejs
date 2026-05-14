/*
 * Optional QuickJS “island” for eval / Function in blue binaries.
 *
 * Lifetime: g_rt / g_ctx are created lazily on first jsc_quickjs_* call and
 * live for the process (see main.cpp QjsGuard for the separate compile-host
 * QuickJS used only while the blue driver runs).
 */

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <quickjs.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

/* quickjs.h defines macros that collide with js_value.h token pasting (JS_BOOL, JS_NULL…) */
#undef JS_UNDEFINED
#undef JS_NULL
#undef JS_FALSE
#undef JS_TRUE
#undef JS_BOOL

#include "../js_value.h"
#include "jsc_runtime.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

/* After #undef of QuickJS's JS_UNDEFINED/JS_NULL macros, js_value.h's JsTag
 * enumerators shadow those names - use explicit JS_MKVAL for QJS values. */
static inline JSValue qjs_undef(void) {
    return JS_MKVAL(JS_TAG_UNDEFINED, 0);
}
static inline JSValue qjs_null(void) {
    return JS_MKVAL(JS_TAG_NULL, 0);
}

/* File-scope so extern "C" helpers and optional control-plane TU can call. */
static JSRuntime* g_rt  = nullptr;
static JSContext* g_ctx = nullptr;

struct JsFnThunk {
    JsValue fn;
};

static JsValue jsval_from_quickjs(JSContext* ctx, JSValueConst qv);

static void ensure_runtime(void) {
    if (g_rt)
        return;
    g_rt = JS_NewRuntime();
    if (!g_rt)
        return;
    g_ctx = JS_NewContext(g_rt);
}

extern "C" void jsc_quickjs_ensure_runtime(void) {
    ensure_runtime();
}

extern "C" JSContext* jsc_quickjs_get_context(void) {
    ensure_runtime();
    return g_ctx;
}

static JSValue jsval_to_quickjs(JSContext* ctx, JsValue jv) {
    switch (jv.tag) {
        case JS_UNDEFINED:
            return qjs_undef();
        case JS_NULL:
            return qjs_null();
        case JS_BOOLEAN:
            return JS_NewBool(ctx, jv.as.boolean ? 1 : 0);
        case JS_NUMBER:
            return JS_NewFloat64(ctx, jv.as.number);
        case JS_STRING: {
            const char* p = jv.as.string ? jv.as.string : "";
            return JS_NewString(ctx, p);
        }
        case JS_BYTES: {
            if (!jv.as.bytes)
                return qjs_undef();
            JSValue ab = JS_NewArrayBufferCopy(ctx, jv.as.bytes->data, jv.as.bytes->len);
            if (JS_IsException(ab))
                return qjs_undef();
            /* Wrap in Uint8Array via globalThis.Uint8Array constructor */
            JSValue g = JS_GetGlobalObject(ctx);
            JSValue u8ctor = JS_GetPropertyStr(ctx, g, "Uint8Array");
            JS_FreeValue(ctx, g);
            if (JS_IsUndefined(u8ctor) || JS_IsException(u8ctor)) {
                JS_FreeValue(ctx, ab);
                return qjs_undef();
            }
            JSValueConst args[1] = { ab };
            JSValue u8 = JS_CallConstructor(ctx, u8ctor, 1, args);
            JS_FreeValue(ctx, u8ctor);
            JS_FreeValue(ctx, ab);
            if (JS_IsException(u8))
                return qjs_undef();
            return u8;
        }
        case JS_FUNCTION:
            if (jv.tag == JS_FUNCTION && jv.as.func) {
                /* We use a custom QuickJS class to manage the lifetime of the C++ thunk. */
                static JSClassID thunk_class_id = 0;
                if (thunk_class_id == 0) {
                    JS_NewClassID(&thunk_class_id);
                    static JSClassDef thunk_class = {
                        .class_name = "BlueThunk",
                        .finalizer = [](JSRuntime* /*rt*/, JSValue val) {
                            auto* th = (JsFnThunk*)JS_GetOpaque(val, thunk_class_id);
                            if (th) {
                                js_dispose_value(&th->fn);
                                delete th;
                            }
                        },
                        .gc_mark = nullptr,
                        .call = nullptr,
                        .exotic = nullptr
                    };
                    JS_NewClass(JS_GetRuntime(ctx), thunk_class_id, &thunk_class);
                }

                auto* thunk = new JsFnThunk{js_clone_value(jv)};
                JSValue obj = JS_NewObjectProtoClass(ctx, qjs_null(), thunk_class_id);
                JS_SetOpaque(obj, thunk);

                auto bridge = [](JSContext* bctx, JSValueConst this_val, int argc,
                                 JSValueConst* argv, int magic,
                                 JSValue* func_data) -> JSValue {
                    (void)this_val;
                    (void)magic;
                    auto* th = (JsFnThunk*)JS_GetOpaque(func_data[0], thunk_class_id);
                    if (!th || th->fn.tag != JS_FUNCTION || !th->fn.as.func)
                        return qjs_undef();
                    std::vector<JsValue> jargv((size_t)argc);
                    for (int i = 0; i < argc; ++i)
                        jargv[(size_t)i] = jsval_from_quickjs(bctx, argv[i]);
                    JsValue out = js_call_argv(th->fn, argc,
                                               argc ? jargv.data() : nullptr);
                    for (int i = 0; i < argc; ++i)
                        js_dispose_value(&jargv[(size_t)i]);
                    JSValue qout = jsval_to_quickjs(bctx, out);
                    js_dispose_value(&out);
                    return qout;
                };
                JSValue data[1] = { obj };
                return JS_NewCFunctionData(ctx, bridge, 0, 0, 1, data);
            }
            return qjs_undef();
        case JS_OBJECT: {
            if (!jv.as.obj)
                return qjs_undef();
            JSValue o = JS_NewObject(ctx);
            for (int i = 0; i < jv.as.obj->count; ++i) {
                const char* k = jv.as.obj->props[i].key;
                JS_SetPropertyStr(ctx, o, k,
                                  jsval_to_quickjs(ctx, jv.as.obj->props[i].value));
            }
            return o;
        }
    }
    return qjs_undef();
}

extern "C" JSValue jsc_quickjs_jsvalue_to_qjs(JsValue jv) {
    ensure_runtime();
    if (!g_ctx)
        return qjs_undef();
    return jsval_to_quickjs(g_ctx, jv);
}

static JsValue jsval_from_quickjs(JSContext* ctx, JSValueConst qv) {
    if (JS_IsUndefined(qv) || JS_IsUninitialized(qv))
        return js_undefined();
    if (JS_IsNull(qv))
        return js_null();

    if (JS_IsBool(qv))
        return js_bool(JS_ToBool(ctx, qv) ? 1 : 0);

    if (JS_IsNumber(qv)) {
        double d = 0.0;
        int err = JS_ToFloat64(ctx, &d, qv);
        if (err != 0) {
            JS_FreeValue(ctx, JS_GetException(ctx));
            return js_undefined();
        }
        return js_num(d);
    }

    if (JS_IsString(qv)) {
        const char* s = JS_ToCString(ctx, qv);
        JsValue out = js_str(s ? s : "");
        if (s)
            JS_FreeCString(ctx, s);
        return out;
    }

    if (JS_IsFunction(ctx, qv))
        return js_undefined();

    JSValue js = JS_JSONStringify(ctx, qv, qjs_undef(), qjs_undef());
    if (JS_IsException(js)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return js_undefined();
    }
    const char* jc = JS_ToCString(ctx, js);
    std::string jtxt = jc ? jc : "";
    if (jc)
        JS_FreeCString(ctx, jc);
    JS_FreeValue(ctx, js);

    if (!jtxt.empty()) {
        JSValue pj =
            JS_ParseJSON(ctx, jtxt.c_str(), jtxt.size(), "<from-qjs-json>");
        if (!JS_IsException(pj)) {
            JsValue conv = jsval_from_quickjs(ctx, pj);
            JS_FreeValue(ctx, pj);
            return conv;
        }
        JS_FreeValue(ctx, JS_GetException(ctx));
        return js_str(jtxt.c_str());
    }

    return js_undefined();
}

extern "C" JsValue jsc_quickjs_qjs_to_jsvalue(JSValueConst qv) {
    ensure_runtime();
    if (!g_ctx)
        return js_undefined();
    return jsval_from_quickjs(g_ctx, qv);
}

extern "C" void jsc_quickjs_pump_microtasks(void) {
    if (!g_rt)
        return;
    JSContext* pctx = nullptr;
    while (true) {
        int r = JS_ExecutePendingJob(g_rt, &pctx);
        if (r <= 0)
            break;
    }
    /* Run GC to break cycles created by req/res/socket shims */
    JS_RunGC(g_rt);
}

extern "C" JsValue jsc_quickjs_eval_from_jsvalue(JsValue source,
                                                const char* filename_hint) {
    ensure_runtime();
    if (!g_ctx || source.tag != JS_STRING || !source.as.string)
        return js_undefined();

    const char* code = source.as.string;
    size_t len       = strlen(code);
    std::string fn   = filename_hint ? filename_hint : "<eval>";

    JSValue res = JS_Eval(g_ctx, code, len, fn.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(res)) {
        JSValue ex = JS_GetException(g_ctx);
        const char* msg = JS_ToCString(g_ctx, ex);
        std::fprintf(stderr, "blue: QuickJS eval error: %s\n", msg ? msg : "?");
        if (msg)
            JS_FreeCString(g_ctx, msg);
        JS_FreeValue(g_ctx, ex);
        return js_undefined();
    }

    JsValue out = jsval_from_quickjs(g_ctx, res);
    JS_FreeValue(g_ctx, res);
    jsc_quickjs_pump_microtasks();
    return out;
}

extern "C" JsValue jsc_quickjs_function_construct(int argc, const JsValue* argv) {
    ensure_runtime();
    if (!g_ctx || !argv || argc < 1)
        return js_undefined();

    if (argc == 1 && argv[0].tag == JS_STRING && argv[0].as.string)
        return jsc_quickjs_eval_from_jsvalue(argv[0], "<Function-single>");
    if (argc < 2)
        return js_undefined();

    std::string body;
    {
        char* b = js_to_cstr(argv[argc - 1]);
        body    = b ? b : "";
        free(b);
    }

    std::vector<std::string> params((size_t)argc - 1);
    for (int i = 0; i < argc - 1; ++i) {
        char* p     = js_to_cstr(argv[i]);
        params[i] = p ? p : "";
        free(p);
    }

    std::string plist;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i)
            plist += ",";
        plist += params[i];
    }

    std::string expr = "(function(" + plist + "){\n" + body + "\n})";

    JSValue rf =
        JS_Eval(g_ctx, expr.c_str(), expr.size(), "<Function>",
                JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(rf)) {
        JSValue ex = JS_GetException(g_ctx);
        const char* msg = JS_ToCString(g_ctx, ex);
        std::fprintf(stderr, "blue: QuickJS Function() error: %s\n",
                     msg ? msg : "?");
        if (msg)
            JS_FreeCString(g_ctx, msg);
        JS_FreeValue(g_ctx, ex);
        return js_undefined();
    }

    if (JS_IsFunction(g_ctx, rf)) {
        std::fprintf(
            stderr,
            "blue: note: values returned from Function()/new Function() are "
            "QuickJS closures; callers that only speak JsValue will see "
            "undefined unless the result serializes.\n");
    }

    JsValue out = jsval_from_quickjs(g_ctx, rf);
    JS_FreeValue(g_ctx, rf);
    jsc_quickjs_pump_microtasks();
    return out;
}
