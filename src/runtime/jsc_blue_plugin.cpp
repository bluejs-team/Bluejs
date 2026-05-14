#include "jsc_blue_plugin.h"
#include "blue_plugin.h"

#if defined(JSC_QJS_CONTROL_PLANE) || defined(JSC_HYBRID)

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <quickjs.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

struct BlueLoadedPlugin {
#ifdef _WIN32
    HMODULE handle;
#else
    void* handle;
#endif
    std::string path;
};

struct BlueRegisteredFunction {
    JSContext* ctx;
    std::string namespace_name;
    std::string function_name;
    BluePluginFunction fn;
    void* userdata;
};

static std::vector<BlueLoadedPlugin>& blue_plugins(void) {
    static std::vector<BlueLoadedPlugin> plugins;
    return plugins;
}

static JSContext* g_plugin_ctx = nullptr;
static JSClassID blue_plugin_func_class_id = 0;

static BluePluginValue blue_plugin_undefined(void) {
    return BluePluginValue{BLUE_PLUGIN_UNDEFINED, 0.0, 0, nullptr, 0};
}
static BluePluginValue blue_plugin_null(void) {
    return BluePluginValue{BLUE_PLUGIN_NULL, 0.0, 0, nullptr, 0};
}
static BluePluginValue blue_plugin_bool(int value) {
    return BluePluginValue{BLUE_PLUGIN_BOOL, 0.0, value ? 1 : 0, nullptr, 0};
}
static BluePluginValue blue_plugin_number(double value) {
    return BluePluginValue{BLUE_PLUGIN_NUMBER, value, 0, nullptr, 0};
}
static BluePluginValue blue_plugin_string(const char* value) {
    return BluePluginValue{BLUE_PLUGIN_STRING, 0.0, 0, value ? value : "",
                           value ? std::strlen(value) : 0};
}
static BluePluginValue blue_plugin_error(const char* message) {
    return BluePluginValue{BLUE_PLUGIN_ERROR, 0.0, 0, message ? message : "plugin error",
                           message ? std::strlen(message) : 0};
}
static void blue_plugin_log(const char* message) {
    std::fprintf(stderr, "blue plugin: %s\n", message ? message : "");
}

static BluePluginValue qjs_to_plugin_value(JSContext* ctx, JSValueConst value,
                                           std::vector<std::string>& strings) {
    if (JS_IsUndefined(value))
        return blue_plugin_undefined();
    if (JS_IsNull(value))
        return blue_plugin_null();
    if (JS_IsBool(value))
        return blue_plugin_bool(JS_ToBool(ctx, value));
    if (JS_IsNumber(value)) {
        double n = 0.0;
        if (JS_ToFloat64(ctx, &n, value) == 0)
            return blue_plugin_number(n);
        JS_FreeValue(ctx, JS_GetException(ctx));
        return blue_plugin_undefined();
    }
    const char* c = JS_ToCString(ctx, value);
    strings.emplace_back(c ? c : "");
    if (c)
        JS_FreeCString(ctx, c);
    const std::string& s = strings.back();
    return BluePluginValue{BLUE_PLUGIN_STRING, 0.0, 0, s.c_str(), s.size()};
}

static JSValue plugin_value_to_qjs(JSContext* ctx, const BluePluginValue& value) {
    switch (value.type) {
        case BLUE_PLUGIN_UNDEFINED:
            return JS_UNDEFINED;
        case BLUE_PLUGIN_NULL:
            return JS_NULL;
        case BLUE_PLUGIN_BOOL:
            return JS_NewBool(ctx, value.boolean ? 1 : 0);
        case BLUE_PLUGIN_NUMBER:
            return JS_NewFloat64(ctx, value.number);
        case BLUE_PLUGIN_STRING:
            return JS_NewStringLen(ctx, value.string ? value.string : "",
                                   value.string ? value.string_len : 0);
        case BLUE_PLUGIN_ERROR:
            return JS_ThrowInternalError(ctx, "%s", value.string ? value.string : "plugin error");
    }
    return JS_UNDEFINED;
}

static JSValue qjs_plugin_call(JSContext* ctx, JSValueConst this_val, int argc,
                               JSValueConst* argv, int magic,
                               JSValue* func_data) {
    (void)this_val;
    (void)magic;
    BlueRegisteredFunction* reg =
        (BlueRegisteredFunction*)JS_GetOpaque(func_data[0], blue_plugin_func_class_id);
    if (!reg || !reg->fn)
        return JS_ThrowInternalError(ctx, "invalid Blue plugin function");

    std::vector<std::string> string_storage;
    string_storage.reserve((size_t)argc);
    std::vector<BluePluginValue> args((size_t)argc);
    for (int i = 0; i < argc; ++i)
        args[(size_t)i] = qjs_to_plugin_value(ctx, argv[i], string_storage);

    BluePluginCallContext call_ctx{reg->userdata};
    BluePluginValue out = reg->fn(&call_ctx, argc, args.empty() ? nullptr : args.data());
    return plugin_value_to_qjs(ctx, out);
}

static void blue_plugin_func_finalizer(JSRuntime* rt, JSValue value) {
    (void)rt;
    auto* reg = (BlueRegisteredFunction*)JS_GetOpaque(value, blue_plugin_func_class_id);
    delete reg;
}

static void ensure_plugin_class(JSContext* ctx) {
    if (blue_plugin_func_class_id != 0)
        return;
    JS_NewClassID(&blue_plugin_func_class_id);
    static JSClassDef cls = {
        .class_name = "BluePluginFunction",
        .finalizer = blue_plugin_func_finalizer,
        .gc_mark = nullptr,
        .call = nullptr,
        .exotic = nullptr
    };
    JS_NewClass(JS_GetRuntime(ctx), blue_plugin_func_class_id, &cls);
}

static void host_define_function(const char* namespace_name,
                                 const char* function_name,
                                 BluePluginFunction fn,
                                 int arity,
                                 void* userdata) {
    JSContext* ctx = g_plugin_ctx;
    if (!ctx || !namespace_name || !function_name || !fn)
        return;
    ensure_plugin_class(ctx);

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ns = JS_GetPropertyStr(ctx, global, namespace_name);
    if (JS_IsException(ns) || JS_IsUndefined(ns) || JS_IsNull(ns)) {
        JS_FreeValue(ctx, ns);
        ns = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, global, namespace_name, JS_DupValue(ctx, ns));
    }

    auto* reg = new BlueRegisteredFunction{
        ctx,
        namespace_name,
        function_name,
        fn,
        userdata
    };
    JSValue holder = JS_NewObjectProtoClass(ctx, JS_NULL, blue_plugin_func_class_id);
    JS_SetOpaque(holder, reg);
    JSValue data[1] = { holder };
    JSValue fnv = JS_NewCFunctionData(ctx, qjs_plugin_call, arity < 0 ? 0 : arity,
                                      0, 1, data);
    JS_SetPropertyStr(ctx, ns, function_name, fnv);
    /* QuickJS keeps func_data values with the function. Do not release holder
     * here; its finalizer owns the BlueRegisteredFunction. */
    JS_FreeValue(ctx, ns);
    JS_FreeValue(ctx, global);
}

static BluePluginHost make_host(void) {
    BluePluginHost host{};
    host.api_version = BLUE_PLUGIN_API_VERSION;
    host.define_function = host_define_function;
    host.make_undefined = blue_plugin_undefined;
    host.make_null = blue_plugin_null;
    host.make_bool = blue_plugin_bool;
    host.make_number = blue_plugin_number;
    host.make_string = blue_plugin_string;
    host.make_error = blue_plugin_error;
    host.log = blue_plugin_log;
    return host;
}

static BluePluginHost& plugin_host(void) {
    static BluePluginHost host = make_host();
    return host;
}

int jsc_blue_plugin_load(JSContext* ctx, const char* path_utf8) {
    if (!ctx || !path_utf8 || !*path_utf8)
        return 0;

#ifdef _WIN32
    HMODULE handle = LoadLibraryA(path_utf8);
    if (!handle) {
        std::fprintf(stderr, "blue: plugin load failed: %s\n", path_utf8);
        return 0;
    }
    auto init = (BluePluginInitFn)GetProcAddress(handle, "blue_plugin_init");
#else
    void* handle = dlopen(path_utf8, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        std::fprintf(stderr, "blue: plugin load failed: %s: %s\n",
                     path_utf8, dlerror());
        return 0;
    }
    auto init = (BluePluginInitFn)dlsym(handle, "blue_plugin_init");
#endif
    if (!init) {
        std::fprintf(stderr, "blue: plugin missing blue_plugin_init: %s\n", path_utf8);
#ifdef _WIN32
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        return 0;
    }

    g_plugin_ctx = ctx;
    int ok = init(&plugin_host());
    g_plugin_ctx = nullptr;
    if (!ok) {
        std::fprintf(stderr, "blue: plugin init failed: %s\n", path_utf8);
#ifdef _WIN32
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        return 0;
    }

    blue_plugins().push_back(BlueLoadedPlugin{handle, path_utf8});
    return 1;
}

static JSValue qjs_blue_plugin_load(JSContext* ctx, JSValueConst this_val,
                                    int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1)
        return JS_NewBool(ctx, 0);
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_NewBool(ctx, 0);
    int ok = jsc_blue_plugin_load(ctx, path);
    JS_FreeCString(ctx, path);
    return JS_NewBool(ctx, ok ? 1 : 0);
}

void jsc_blue_plugin_install_js(JSContext* ctx, void* blue_object) {
    if (!ctx || !blue_object)
        return;
    JSValue Blue = JS_DupValue(ctx, *(JSValue*)blue_object);
    JSValue Plugin = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, Plugin, "load",
                      JS_NewCFunction(ctx, qjs_blue_plugin_load, "load", 1));
    JS_SetPropertyStr(ctx, Blue, "Plugin", Plugin);
    JS_FreeValue(ctx, Blue);
}

#else

int jsc_blue_plugin_load(struct JSContext* ctx, const char* path_utf8) {
    (void)ctx;
    (void)path_utf8;
    return 0;
}

void jsc_blue_plugin_install_js(struct JSContext* ctx, void* blue_object) {
    (void)ctx;
    (void)blue_object;
}

#endif
