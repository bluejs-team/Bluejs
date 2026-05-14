/*
 * True hybrid: AOT binary embeds an npm island (QuickJS). AOT main calls
 * jsc_hybrid_boot_island once; optional jsc_hybrid_island_call_json for FFI.
 */

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <quickjs.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#undef JS_UNDEFINED
#undef JS_NULL
#undef JS_FALSE
#undef JS_TRUE
#undef JS_BOOL

#include "../js_value.h"
#include "jsc_runtime.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#if defined(JSC_HYBRID) && defined(JSC_RUNTIME_QUICKJS) && \
    defined(JSC_RUNTIME_NODE_IO) && defined(JSC_HAVE_UV)

static void blue_write_boot_metric_ms(long long ms) {
    const char* p = std::getenv("BLUE_BOOT_METRICS_FILE");
    if (!p || !*p)
        return;
    std::fflush(stdout);
    std::fflush(stderr);
    if (FILE* f = std::fopen(p, "w")) {
        std::fprintf(f, "%lld\n", ms);
        std::fclose(f);
    }
}

static inline JSValue qjs_undef(void) {
    return JS_MKVAL(JS_TAG_UNDEFINED, 0);
}

extern "C" void jsc_qjs_install_control_plane_shims(JSContext* ctx);

extern "C" JsValue jsc_quickjs_qjs_to_jsvalue(JSValueConst qv);
extern "C" JSValue jsc_quickjs_jsvalue_to_qjs(JsValue jv);



static JSValue qjs_hybrid_call_aot_json(JSContext* ctx, JSValueConst this_val,
                                        int argc, JSValueConst* argv) {
    (void)this_val;
    if (!g_aot_dispatch || argc < 1)
        return qjs_undef();
    if (argc >= 2 && !JS_IsString(argv[1])) {
        return JS_ThrowTypeError(
            ctx,
            "🛑 Blue Runtime Error: Bridge Type Mismatch\n"
            "jsc_hybrid_island_call_json('%s', payload) failed.\n"
            "The C++ receiver expected payload.content to be a String, but "
            "received an Object.",
            "callAot");
    }
    const char* fn_name = JS_ToCString(ctx, argv[0]);
    const char* payload = (argc >= 2) ? JS_ToCString(ctx, argv[1]) : nullptr;
    JsValue out = js_undefined();
    try {
        out = g_aot_dispatch(fn_name ? fn_name : "", payload ? payload : "");
    } catch (const std::exception& ex) {
        if (fn_name)
            JS_FreeCString(ctx, fn_name);
        if (payload)
            JS_FreeCString(ctx, payload);
        return JS_ThrowInternalError(
            ctx,
            "🛑 Blue Runtime Error: Bridge Type Mismatch\n"
            "jsc_hybrid_island_call_json('%s', payload) failed.\n"
            "Bridge dispatch threw: %s",
            "callAot", ex.what());
    } catch (...) {
        if (fn_name)
            JS_FreeCString(ctx, fn_name);
        if (payload)
            JS_FreeCString(ctx, payload);
        return JS_ThrowInternalError(
            ctx,
            "🛑 Blue Runtime Error: Bridge Type Mismatch\n"
            "jsc_hybrid_island_call_json('%s', payload) failed.\n"
            "Bridge dispatch threw an unknown native exception.",
            "callAot");
    }
    if (fn_name)
        JS_FreeCString(ctx, fn_name);
    if (payload)
        JS_FreeCString(ctx, payload);
    JSValue qout = jsc_quickjs_jsvalue_to_qjs(out);
    js_dispose_value(&out);
    return qout;
}

static void install_aot_bridge(JSContext* ctx) {
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue aot = JS_NewObject(ctx);
    JS_SetPropertyStr(
        ctx, aot, "callJson",
        JS_NewCFunction(ctx, qjs_hybrid_call_aot_json, "callJson", 2));
    JS_SetPropertyStr(ctx, g, "__BLUE_AOT", aot);
    JS_FreeValue(ctx, g);
}


extern "C" void jsc_hybrid_boot_island(const unsigned char* bundle, size_t len) {
    auto tIsland0 = std::chrono::steady_clock::now();
    jsc_quickjs_ensure_runtime();
    JSContext* ctx = jsc_quickjs_get_context();
    if (!ctx || !bundle || len == 0) {
        fprintf(stderr, "blue: hybrid: missing context or island bundle\n");
        return;
    }
    jsc_qjs_install_control_plane_shims(ctx);
    install_aot_bridge(ctx);
    /* Blue.island + Blue.callAot exist before the island bundle runs (ergonomic API). */
    static const char kIslandBluePreamble[] =
        "(function(){var g=globalThis;"
        "g.__BLUE_ISLAND=g.__BLUE_ISLAND||{};"
        "var A=g.Blue||(g.Blue={});"
        "A.island=g.__BLUE_ISLAND;"
        "A.aot=g.__BLUE_AOT;"
        "A.callAot=function(n,p){return g.__BLUE_AOT.callJson(n,"
        "p===void 0?\"\":String(p));};})();";
    JSValue pre = JS_Eval(ctx, kIslandBluePreamble, sizeof(kIslandBluePreamble) - 1,
                          "<blue-hybrid-blue-preamble.js>",
                          JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(pre)) {
        JSValue ex = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, ex);
        fprintf(stderr, "blue: hybrid Blue preamble error: %s\n",
                msg ? msg : "?");
        if (msg)
            JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, ex);
    }
    JS_FreeValue(ctx, pre);

    JSValue ret = JS_Eval(ctx, (const char*)bundle, len, "blue-hybrid-island.js",
                          JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ret)) {
        JSValue ex = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, ex);
        fprintf(stderr, "blue: hybrid island eval error: %s\n", msg ? msg : "?");
        JSValue st = JS_GetPropertyStr(ctx, ex, "stack");
        if (!JS_IsException(st) && !JS_IsUndefined(st)) {
            const char* s = JS_ToCString(ctx, st);
            if (s) {
                fprintf(stderr, "blue: hybrid island stack: %s\n", s);
                JS_FreeCString(ctx, s);
            }
            JS_FreeValue(ctx, st);
        } else if (!JS_IsException(st)) {
            JS_FreeValue(ctx, st);
        }
        if (msg)
            JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, ex);
    }
    JS_FreeValue(ctx, ret);
    jsc_quickjs_pump_microtasks();
    /* Island calls http.listen(); handles are on libuv but nothing runs the
     * loop until main's epilogue. Pump now so accept(2) works before any later
     * blocking step (e.g. WebView init + fetches to 127.0.0.1). */
    jsc_uv_run_loop_until_idle();

    auto tIsland1 = std::chrono::steady_clock::now();
    long long elapsedMs =
        (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
            tIsland1 - tIsland0)
            .count();
    blue_write_boot_metric_ms(elapsedMs);
}

extern "C" JsValue jsc_hybrid_island_call_json(const char* export_name,
                                               const char* json_in_utf8) {
    if (!export_name || !export_name[0])
        return js_undefined();
    jsc_quickjs_ensure_runtime();
    JSContext* ctx = jsc_quickjs_get_context();
    if (!ctx)
        return js_undefined();

    JSValue g      = JS_GetGlobalObject(ctx);
    JSValue island = JS_GetPropertyStr(ctx, g, "__BLUE_ISLAND");
    JS_FreeValue(ctx, g);
    if (JS_IsException(island) || JS_IsUndefined(island)) {
        if (!JS_IsException(island))
            JS_FreeValue(ctx, island);
        else
            JS_FreeValue(ctx, JS_GetException(ctx));
        fprintf(stderr,
                "🛑 Blue Runtime Error: Bridge\n"
                "globalThis.__BLUE_ISLAND missing while dispatching island call.\n");
        return js_undefined();
    }

    JSValue fn = JS_GetPropertyStr(ctx, island, export_name);
    JS_FreeValue(ctx, island);
    if (!JS_IsFunction(ctx, fn)) {
        JS_FreeValue(ctx, fn);
        fprintf(stderr, "🛑 Blue Runtime Error: Bridge\n"
                        "__BLUE_ISLAND.%s is not a function\n",
                export_name);
        return js_undefined();
    }

    JSValue arg = JS_NewString(ctx, json_in_utf8 ? json_in_utf8 : "");
    JSValueConst args[1] = {arg};
    JSValue      ret     = JS_Call(ctx, fn, qjs_undef(), 1, args);
    JS_FreeValue(ctx, arg);
    JS_FreeValue(ctx, fn);
    if (JS_IsException(ret)) {
        JSValue ex = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, ex);
        fprintf(stderr, "🛑 Blue Runtime Error: Bridge\n"
                        "hybrid island call error: %s\n", msg ? msg : "?");
        if (msg)
            JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, ex);
        return js_undefined();
    }
    JsValue out = jsc_quickjs_qjs_to_jsvalue(ret);
    JS_FreeValue(ctx, ret);
    jsc_quickjs_pump_microtasks();
    return out;
}

#elif defined(JSC_HYBRID)


extern "C" void jsc_hybrid_boot_island(const unsigned char* bundle, size_t len) {
    (void)bundle;
    (void)len;
    fprintf(stderr,
            "blue: hybrid requires QuickJS + libuv + JSC_RUNTIME_NODE_IO "
            "(same as npm island shims)\n");
}

extern "C" JsValue jsc_hybrid_island_call_json(const char* export_name,
                                               const char* json_in_utf8) {
    (void)export_name;
    (void)json_in_utf8;
    return js_undefined();
}

#endif
