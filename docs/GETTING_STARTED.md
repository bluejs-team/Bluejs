# Getting started

## Try First In Codespaces

The recommended first-run path is the hosted playground:

[Open Bluejs Playground in Codespaces](https://codespaces.new/bluejs-team/bluejs-playground)

It opens with Bluejs, C++ build tools, Node.js, and runnable examples already
set up. The Windows and Linux local builds are experimental, so try Codespaces
first if you want the fastest path.

## 1. Install Locally

Local Windows and Linux builds are experimental. Use Codespaces first if you
just want to try Bluejs.

Linux:

```bash
curl -fsSL https://bluejs.dev/install.sh | bash
```

Windows:

Download and run the installer:

```text
https://bluejs.dev/downloads/blue-windows-x86_64-setup.exe
```

After installing locally, open a new terminal and verify:

```bash
blue --version
```

### Dependencies

| What | Why | Ubuntu/Debian |
|------|-----|--------------|
| C++ compiler (g++ / clang++) | Compiles generated code | `sudo apt install build-essential` |
| `pkg-config` | Finds libraries at link time | `sudo apt install pkg-config` |
| **libuv** dev package | HTTP servers, timers, event loop | `sudo apt install libuv1-dev` |
| **GTK 3 + WebKit2GTK** dev packages | Native window | `sudo apt install libgtk-3-dev libwebkit2gtk-4.1-dev` |
| **Node.js** | npm bundling for hybrid islands | [nodejs.org](https://nodejs.org) |

On Linux, install build dependencies as needed:

```bash
sudo apt install build-essential pkg-config libuv1-dev libgtk-3-dev libwebkit2gtk-4.1-dev
```

---

## 2. Your first app

Create `hello.js`:

```js
window.open("<h1>Hello from Blue</h1>", "My App", 600, 400);
```

Compile and run:

```bash
blue -compile hello.js -o hello
./hello
```

A native window opens with your HTML.

---

## 3. Scaffold a project

For a full hybrid project with AOT entry, island, and embedded UI:

```bash
blue -init myapp
cd myapp
```

This creates:

```
myapp/
  blue.config.json      # build configuration
  src/main.js            # AOT entry - compiled to C++
  src/island.js          # QuickJS island - npm-capable, full ES2020+
  public/index.html      # embedded in the binary at build time
```

Build and run:

```bash
blue -build myapp -o /tmp/myapp
/tmp/myapp
```

Or in one step from the parent directory:

```bash
blue -init myapp --build
blue -build myapp -o /tmp/myapp
```

---

## 4. What goes where

| File | Engine | Use for |
|------|--------|---------|
| `src/main.js` | **AOT** - compiled to C++ | Native windows, filesystem, classes, business logic |
| `src/island.js` | **QuickJS** - interpreted | HTTP servers, npm packages, `async`/`await`, dynamic logic |

**AOT** supports a wide JS subset: classes, closures, prototype inheritance, destructuring, template literals, standard array/string/object methods. See [Strict AOT](STRICT_AOT.md) for the complete list.

**Island** runs full ES2020+ inside embedded QuickJS. npm packages work via esbuild bundling. The installer and Codespaces setup install Bluejs' esbuild helper; project-specific npm dependencies still need `npm install` in that project.

**Rule:** `require("npm-package")` only works in the island. The AOT entry supports `require("fs")`, `require("path")`, and other Node built-in shims.

**npm compatibility in the island:** pure JavaScript packages generally work. Packages that use native addons, `worker_threads`, `child_process`, or deep Node.js internals may not work or may be partially supported. This is an active area of development - compatibility improves with each release. See [Hybrid Mode](HYBRID.md) for the full breakdown.

---

## 5. Next steps

- [API Reference](API.md) - `Blue.Window`, `Blue.Dialog`, `Blue.Clipboard`, WebView bridge, Node shims
- [Hybrid Mode](HYBRID.md) - AOT ↔ Island communication via `Blue.callAot()` / `Blue.callIsland()`
- [Configuration](CONFIGURATION.md) - full `blue.config.json` field reference
- [Troubleshooting](TROUBLESHOOTING.md) - error messages and fixes
- [`examples/`](../examples/) - complete runnable projects
