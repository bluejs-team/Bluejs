# Troubleshooting

Quick reference for the most common Blue errors and how to fix them.

---

## Compiler errors

### `blue: compiler binary not found (expected blue_bin)`

The `blue` shell script cannot find `blue_bin` next to it.

**Fix:**
```bash
blue --version
```

If this came from a local install, reinstall Bluejs or verify the tarball
extracted both `blue` and `blue_bin` into the same directory.

---

### `blue: C++ compiler failed. (hint run with --print-c to inspect)`

The generated C++ did not compile. Common causes:

- Missing `g++` / `c++`: install `build-essential` (`sudo apt-get install build-essential`).
- Missing system headers for a runtime library (libuv, GTK, WebKit).
- An unsupported JS pattern produced invalid C++.

**Debug steps:**
```bash
# See the generated C++
blue -compile yourfile.js --print-c | less

# See the full compiler command and errors
blue -compile yourfile.js -o /tmp/out 2>&1
```

---

### `pkg-config: command not found` (or link failures for libuv / GTK / WebKit)

Blue uses `pkg-config` to find library flags at link time.

**Fix:**
```bash
sudo apt-get install pkg-config
sudo apt-get install libuv1-dev            # for HTTP server / hybrid island
sudo apt-get install libgtk-3-dev libwebkit2gtk-4.1-dev   # for WebView apps
```

---

### `⚠️ Blue Build Warning: Unimplemented JavaScript Feature`

A JS feature is not supported in AOT mode. The code lowers to a stub.

**Fix options:**
1. Move the code to `src/island.js` - the island runs full QuickJS with npm support.
2. Rewrite using AOT-compatible patterns (see `docs/STRICT_AOT.md`).
3. Set `BLUE_STRICT_UNSUPPORTED=1` to turn warnings into hard errors during development.

---

### `blue: hybrid builds require "quickjsIsland"` 

`blue.config.json` has `"hybrid": true` but `"quickjsIsland"` is missing or misspelled.

**Fix:** Add the island path to `blue.config.json`:
```json
{
  "hybrid": true,
  "quickjsIsland": "src/island.js"
}
```

---

### `blue: entry file not found`

The `entry` path in `blue.config.json` does not exist relative to the project directory.

**Fix:** Check the path. Default is `"entry": "src/main.js"`. Run `blue -init <dir>` to scaffold a correct project structure.

---

### `esbuild still missing after npm install`

The hybrid island bundler (esbuild) is not set up.

**Fix:**
```bash
blue --version
```

If you installed Bluejs locally and still see this error, rerun the installer.
On Windows, you can also run this once from an administrator terminal:

```bat
cd "C:\Program Files\Bluejs\tools\jsc-npm-bundle"
npm install
```

---

---

### npm package throws at runtime or behaves unexpectedly

npm packages run inside the QuickJS island, which is not a full Node.js runtime. Compatibility varies by package.

**Common causes:**
- Package calls a Node.js built-in not covered by Blue's shims (`crypto.subtle`, `worker_threads`, `child_process`, `net.Socket` directly, etc.)
- Package uses native addons (`.node` files) - these cannot run in QuickJS
- Package inspects `process.versions`, `process.binding`, or other Node.js internals that Blue does not emulate

**What to try:**
1. Check if the package has a browser-compatible build - set `"bundlePlatform": "browser"` in `blue.config.json` to target the browser bundle instead
2. Look for a lighter alternative that is pure JavaScript
3. File an issue - shim coverage is being expanded and your package may be easy to support

npm package compatibility is an active area of development. What doesn't work today may work in a future release.

---

## Runtime errors

### Binary works on the build machine but not on another machine

Blue binaries dynamically link against platform libraries. What the target machine needs:

| Binary type | Required on target |
|---|---|
| AOT only (no window, no HTTP) | Nothing - `libc` is on every Linux system |
| Hybrid / HTTP | `libuv1` unless libuv was statically linked at compile time (Blue does this automatically when `libuv.a` is present) |
| WebView (`window.open`) | `libgtk-3-0 libwebkit2gtk-4.1-0` and dependencies |

**Fix for WebView apps:** install the GTK/WebKit runtime on the target, or distribute as an [AppImage](https://appimage.org) which bundles these libraries.

**Fix for HTTP/hybrid apps:** install `libuv-dev` on the *build* machine so Blue can link libuv statically, or install `libuv1` on the *target* machine.

---

### App exits immediately with no output

Likely causes:
- `window.open()` called without a display - run with `DISPLAY=:0` or inside a desktop session.
- A startup exception was thrown and caught silently - add `console.log` calls to isolate.

---

### `WebView builds need gtk+-3.0 + webkit2gtk-4.x dev packages`

The GTK/WebKit libraries are not installed on the build machine.

**Fix:**
```bash
sudo apt-get install libgtk-3-dev libwebkit2gtk-4.1-dev
# If 4.1 is unavailable, try:
sudo apt-get install libwebkit2gtk-4.0-dev
```

---

### `blue: hybrid: missing context or island bundle`

The island bundle is empty or was not generated. This usually means the Bluejs esbuild helper is unavailable, or the project dependencies were not installed.

**Fix:**
```bash
blue --version
cd <project-with-package-json>
npm install
cd ..
blue -build <project>
```

---

### `fs.readdirSync` returns fewer entries than expected

The AOT property bag holds a maximum of 31 file entries. Directories with more entries are truncated.

**Fix:** Move the `readdirSync` call to `src/island.js` where full Node.js `fs` is available.

---

### `Blue.Tray` / `Blue.GlobalShortcut` is undefined

These APIs were removed in v1.0. Delete any calls to them from your code.

---

## Build / packaging

### `.deb` postinst shows missing dependencies

```
sudo apt update && sudo apt install -y <listed packages>
```

The postinst script lists exactly which packages to install.

---

### `parse error in ...js: Error: Line N: Unexpected token`

The JavaScript file contains syntax that esprima (the parser) cannot handle, such as optional chaining (`?.`), nullish coalescing (`??`), or other recent syntax.

**Fix:** Blue runs Babel before parsing to lower modern syntax. If Babel fails in a local install, reinstall Bluejs. If the syntax is truly unsupported, move the code to the island where full modern JS runs.

```bash
blue --version
```
