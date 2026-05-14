# Architecture

## Pipeline

```
 JavaScript source
       │
       ▼
 Babel lowering (QuickJS in compiler)
       │
       ▼
 Esprima AST → Module graph
       │
       ▼
 CEmitter → C++ source
       │
       ▼
 C++ compile + link (runtime, QuickJS, libuv, GTK/WebKit)
       │
       ▼
 Native executable
```

## AOT compilation

The compiler parses JavaScript, resolves modules, and emits C++ code where every JS variable becomes a `JsValue` tagged union. The generated C++ is linked with runtime libraries to produce a standalone binary.

Supported: variables, functions, closures, classes and inheritance (via Babel lowering), prototypes, `this`/`new`, loops, iterators, destructuring, template literals, objects, arrays (with full method suite), string operations, `require("fs")` and other Node built-in shims, `window.open()`.
See [Strict AOT Support](STRICT_AOT.md) for the full list.

Not supported in AOT: `async`/`await`, generators, `eval`, dynamic `require()`, ES module `import`/`export`, regular expressions, `Proxy`/`Reflect` (use the QuickJS island for these).

## Hybrid builds

Hybrid mode produces two artifacts in one binary:

1. **AOT code** - your `entry` file compiled to C++
2. **Island bundle** - your `quickjsIsland` file bundled with esbuild, embedded as bytes, executed in QuickJS

### Boot order

1. Runtime initializes (libuv, QuickJS)
2. Island bundle evaluates (HTTP servers start here)
3. AOT entry runs (native windows open here)

### Build directory

```
myapp/.blue-build/
  main.entry.js           # entry after HTML substitution
  island.bundled.js        # esbuild output for island
  jsc_assets.cpp           # embedded public/ files
  *__blue_tmp.cc          # generated C++ (passed to compiler)
```

## Key source files

| File | Role |
|------|------|
| `src/main.cpp` | CLI driver (`-init`, `-build`, `-compile`), build orchestration |
| `src/emitter.hpp` | AST → C++ code generation |
| `src/parser.hpp` | esprima JSON → ASTNode tree |
| `src/module_resolver.hpp` | CommonJS `require()` graph resolution |
| `src/babel_transform.hpp` | Babel preset-env via embedded QuickJS |
| `src/runtime/jsc_hybrid_runtime.cpp` | Island boot, AOT dispatch |
| `src/runtime/linux/jsc_webview_linux.cpp` | GTK/WebKit2 native window (Linux) |
| `src/runtime/linux/jsc_blue_native_linux.cpp` | `Blue.Window`, `Dialog`, `Clipboard` (Linux) |
| `src/runtime/jsc_node_io.cpp` | libuv-backed filesystem, timers, HTTP |
| `src/runtime/jsc_qjs_node_bridge.cpp` | QuickJS Node.js shims |
| `src/js_value.h` | `JsValue` tagged union |
| `src/js_globals.h` | Built-in objects (`console`, `Math`, `Blue`, `process`) |
