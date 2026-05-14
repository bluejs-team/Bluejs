#pragma once

/*
 * Blue native plugin C ABI.
 *
 * Plugins are dynamic libraries that export:
 *
 *   int blue_plugin_init(const BluePluginHost* host);
 *
 * The ABI intentionally avoids QuickJS and Blue internal headers so plugins can
 * be built with a C compiler and a stable public header.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLUE_PLUGIN_API_VERSION 1

#if defined(_WIN32)
#  define BLUE_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define BLUE_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

typedef enum BluePluginValueType {
    BLUE_PLUGIN_UNDEFINED = 0,
    BLUE_PLUGIN_NULL = 1,
    BLUE_PLUGIN_BOOL = 2,
    BLUE_PLUGIN_NUMBER = 3,
    BLUE_PLUGIN_STRING = 4,
    BLUE_PLUGIN_ERROR = 5
} BluePluginValueType;

typedef struct BluePluginValue {
    BluePluginValueType type;
    double number;
    int boolean;
    const char* string;
    size_t string_len;
} BluePluginValue;

typedef struct BluePluginCallContext {
    void* userdata;
} BluePluginCallContext;

typedef BluePluginValue (*BluePluginFunction)(BluePluginCallContext* ctx,
                                              int argc,
                                              const BluePluginValue* argv);

typedef struct BluePluginHost {
    int api_version;
    void (*define_function)(const char* namespace_name,
                            const char* function_name,
                            BluePluginFunction fn,
                            int arity,
                            void* userdata);
    BluePluginValue (*make_undefined)(void);
    BluePluginValue (*make_null)(void);
    BluePluginValue (*make_bool)(int value);
    BluePluginValue (*make_number)(double value);
    BluePluginValue (*make_string)(const char* value);
    BluePluginValue (*make_error)(const char* message);
    void (*log)(const char* message);
} BluePluginHost;

typedef int (*BluePluginInitFn)(const BluePluginHost* host);

#ifdef __cplusplus
}
#endif
