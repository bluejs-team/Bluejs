# Native Plugins

Blue native plugins are shared libraries written in C or C-compatible C++.
They are loaded by the QuickJS island at runtime with:

```js
Blue.Plugin.load("./path/to/plugin.so");
```

On Windows, use a `.dll`. On macOS, use a `.dylib`.

## ABI

Plugins include:

```c
#include "runtime/blue_plugin.h"
```

Every plugin exports:

```c
BLUE_PLUGIN_EXPORT int blue_plugin_init(const BluePluginHost* host);
```

`blue_plugin_init` should return `1` on success and `0` on failure.

## Registering Functions

Plugins register functions into a JavaScript namespace:

```c
static const BluePluginHost* g_host;

static BluePluginValue add(BluePluginCallContext* ctx, int argc,
                           const BluePluginValue* argv) {
    (void)ctx;
    double a = argc > 0 && argv[0].type == BLUE_PLUGIN_NUMBER ? argv[0].number : 0;
    double b = argc > 1 && argv[1].type == BLUE_PLUGIN_NUMBER ? argv[1].number : 0;
    return g_host->make_number(a + b);
}

BLUE_PLUGIN_EXPORT int blue_plugin_init(const BluePluginHost* host) {
    if (!host || host->api_version != BLUE_PLUGIN_API_VERSION)
        return 0;
    g_host = host;
    host->define_function("MathPlugin", "add", add, 2, 0);
    return 1;
}
```

JavaScript can then call:

```js
Blue.Plugin.load("./math_plugin.so");
console.log(MathPlugin.add(20, 22));
```

## Supported Values

The first ABI version supports:

| Type | C enum | JavaScript |
|---|---|---|
| undefined | `BLUE_PLUGIN_UNDEFINED` | `undefined` |
| null | `BLUE_PLUGIN_NULL` | `null` |
| boolean | `BLUE_PLUGIN_BOOL` | `true` / `false` |
| number | `BLUE_PLUGIN_NUMBER` | `number` |
| string | `BLUE_PLUGIN_STRING` | `string` |
| error | `BLUE_PLUGIN_ERROR` | thrown exception |

Objects, arrays, buffers, callbacks, and async handles are not part of ABI v1.
Use strings, numbers, and JSON text for the first version of a plugin.

## Build Example

Linux:

```bash
cc -shared -fPIC -I/path/to/blue/src plugin.c -o my_plugin.so
```

Windows with MinGW:

```bash
x86_64-w64-mingw32-gcc -shared -I/path/to/blue/src plugin.c -o my_plugin.dll
```

## Lifetime Rules

- Argument strings are valid only during the plugin function call.
- Returned strings are copied into QuickJS immediately.
- Plugin libraries remain loaded for the life of the process.
- Plugins should not store the `BluePluginHost*` across ABI versions unless they
  check `api_version` first.

## Raylib Example

See [examples/raylib-plugin](../examples/raylib-plugin/) for a plugin that wraps
a few raylib calls.
