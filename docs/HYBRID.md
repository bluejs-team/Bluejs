# Hybrid Mode

Hybrid builds combine two JavaScript contexts in one executable:

- **AOT entry** - compiled to C++. Fast, no npm. Use for native windows, filesystem, performance.
- **QuickJS island** - interpreted. npm packages via esbuild. Use for HTTP servers, dynamic logic.

## When to use hybrid

Use hybrid when you need npm packages alongside native features. If your app only needs `window.open` + `fs` + `Blue.*`, a plain AOT build is simpler.

## npm package compatibility

npm packages run inside the QuickJS island via esbuild bundling. **Compatibility varies by package** - this is an active area of development.

**Packages that generally work:**
- Pure JavaScript libraries (lodash, dayjs, marked, zod, etc.)
- HTTP frameworks that don't rely on Node.js internals (basic Express routing, Koa)
- React and other UI libraries (for frontend bundle generation)

**Packages that may not work or are partially supported:**
- Packages that use native addons (`.node` files) - not supported
- Packages that call Node.js built-ins not covered by Blue's shims (e.g. `crypto.subtle`, `worker_threads`, `child_process`)
- Packages that depend on `eval` or dynamic `require()` at runtime
- Packages that inspect `process.versions`, `process.binding`, or other Node internals

**If a package doesn't work:** file an issue - shim coverage is being expanded with each release. As a workaround, the island's `require("fs")`, `require("http")`, `require("path")`, and `require("os")` shims cover the majority of typical server-side use cases.

## Configuration

```json
{
  "entry": "src/main.js",
  "quickjsIsland": "src/island.js",
  "hybrid": true,
  "bundlePlatform": "node"
}
```

## FFI: calling between AOT and Island

### Island → AOT

```js
// In island.js: call a function defined in main.js
var result = Blue.callAot("saveNote", markdownText);
```

`Blue.callAot(name, payload)` calls the named top-level function from your AOT entry. Both argument and return value are strings.

### AOT → Island

```js
// In main.js: call an export defined in island.js
var result = Blue.callIsland("processData", jsonPayload);
```

The island must register the export:

```js
// In island.js
Blue.island.processData = function(payload) {
  return JSON.stringify({ done: true });
};
```

### Available globals

| Global | Where | Description |
|--------|-------|-------------|
| `Blue.callAot(name, payload)` | Island | Call AOT function |
| `Blue.callIsland(name, payload)` | AOT | Call island export |
| `Blue.island` | Island | Export bucket (your functions go here) |
| `Blue.aot` | Island | Alias of the AOT dispatcher |
| `Blue.Window`, `Blue.Dialog`, etc. | Both | Native desktop APIs (see [API Reference](API.md)) |

### Legacy globals

`globalThis.__BLUE_AOT.callJson(name, payload)` and `globalThis.__BLUE_ISLAND` still work but the `Blue.*` form is preferred.

## Rules

1. **No npm in AOT.** Bare `require("npm-package")` in the entry file fails the build. Move npm imports to the island.
2. **Node built-ins in AOT are shims.** `require("fs")`, `require("path")`, etc. work but are simplified implementations - not full Node.js.
3. **npm packages in the island may or may not work.** Pure JS packages usually work; packages with native addons or deep Node.js internals may not. Test early.
4. **Island runs first.** The island evaluates before AOT top-level code, so HTTP servers are listening before `window.open` blocks.
5. **Don't call `window.open` from HTTP handlers.** The native window loop blocks the thread. Open windows from AOT top-level code only.
