# Blue

**Blue compiles JavaScript to standalone native binaries.**

Write JS, ship a single executable with native GUI windows, filesystem access, HTTP servers, and system APIs - no Electron, no Node.js at runtime.

```bash
# Install
curl -fsSL https://bluejs.dev/install.sh | bash

# Write JS
echo 'window.open("<h1>Hello from Blue</h1>", "Hello", 800, 500);' > hello.js

# Compile and run
blue -compile hello.js -o hello && ./hello
```

**[bluejs.dev](https://bluejs.dev) - [Discord](https://discord.gg/P9ym64jZqZ)**

---

## Try it in your browser (preferred)

[![Open in GitHub Codespaces](https://github.com/codespaces/badge.svg)](https://codespaces.new/bluejs-team/bluejs-playground)

The fastest way to get started is the [Bluejs Playground](https://codespaces.new/bluejs-team/bluejs-playground), a GitHub Codespace with everything pre-installed. No setup needed. Local installation on Linux and Windows is also available but currently experimental.

---

## Two modes, one binary

**AOT** - your JavaScript is compiled directly to C++, then to native code. No interpreter at runtime. Supports classes, closures, prototypes, destructuring, template literals, and standard library methods. Use this for native windows, filesystem, and business logic.

**Island** - an embedded QuickJS engine bundled alongside the AOT code. Full ES2020+, async/await, and npm packages via esbuild. Use this for HTTP servers, dynamic logic, and npm dependencies.

Both run in the same process and communicate via `Blue.callAot()` / `Blue.callIsland()`.

---

## Install

| Platform | Method |
|---|---|
| Linux | `curl -fsSL https://bluejs.dev/install.sh \| bash` |
| Linux | [Download .deb (x86_64)](https://bluejs.dev/downloads/blue-linux-amd64.deb) |
| Windows | [Download .zip (x86_64)](https://bluejs.dev/downloads/blue-windows-x86_64.zip) - extract and run `.\install.ps1` |

The installer checks for dependencies and tells you exactly what to install if anything is missing.

### Linux dependencies

```bash
# Required for all builds
sudo apt install build-essential pkg-config

# For native GUI windows (window.open, Blue.Window, Blue.Dialog)
sudo apt install libgtk-3-dev libwebkit2gtk-4.1-dev

# For HTTP servers and hybrid island builds
sudo apt install libuv1-dev nodejs
```

### Windows dependencies

A C++ compiler is required. Install via `winget install MinGW.MinGW` or MSVC Build Tools. For native GUI windows, the [WebView2 SDK](https://developer.microsoft.com/en-us/microsoft-edge/webview2/) is also needed.

---

## Quick start

```bash
# Single file - AOT compile
blue -compile hello.js -o hello && ./hello

# Full hybrid project
blue -init myapp
blue -build myapp -o myapp && ./myapp
```

---

## Examples

| Example | What it shows |
|---|---|
| [hello-webview](examples/hello-webview/) | Open a native window in 15 lines |
| [aot-math](examples/aot-math/) | AOT loops and number crunching |
| [markdown-notes-demo](examples/markdown-notes-demo/) | WebView + filesystem via blue:// bridge |
| [http-server](examples/http-server/) | Hybrid HTTP server with Blue.System |
| [react-init-hybrid](examples/react-init-hybrid/) | React frontend bundled into the binary |
| [full-api-demo](examples/full-api-demo/) | All Blue.* APIs in one app |

---

## Native APIs

```js
// Windows
Blue.Window.setTitle("My App");
Blue.Window.setSize(1024, 768);
Blue.Window.center();

// File dialogs
var path = Blue.Dialog.showOpenDialog("Open file", "*.txt", false);
var save = Blue.Dialog.showSaveDialog("Save as", "output.txt");

// Clipboard
Blue.Clipboard.writeText("hello");
var text = Blue.Clipboard.readText();

// Shell
var result = Blue.Process.exec("ls -la");
console.log(result.stdout);
```

---

## Documentation

| | |
|---|---|
| [Getting Started](docs/GETTING_STARTED.md) | Installation, first app, project layout |
| [CLI Reference](docs/CLI.md) | All commands and flags |
| [Configuration](docs/CONFIGURATION.md) | blue.config.json fields |
| [API Reference](docs/API.md) | window.open, Blue.*, Node shims, WebView bridge |
| [Hybrid Mode](docs/HYBRID.md) | Dual-entry builds, AOT and Island communication |
| [Strict AOT](docs/STRICT_AOT.md) | Supported JS subset |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Common errors and fixes |
| [Benchmarks](docs/BENCHMARKS.md) | Performance vs Node.js |

---

## License

Blue is licensed under the Polyform Small Business License 1.0.0. See
[LICENSE](LICENSE), [NOTICE](NOTICE), and
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

---

## npm compatibility

The island runs embedded QuickJS, not Node.js. Pure JavaScript packages are most likely to work. Packages that rely on native addons, `worker_threads`, `child_process`, or Node.js internals may not work. Always test compatibility before shipping.

---

## Community

- **Discord:** [discord.gg/P9ym64jZqZ](https://discord.gg/P9ym64jZqZ)
- **Issues:** [github.com/bluejs-team/Bluejs/issues](https://github.com/bluejs-team/Bluejs/issues)
- **Website:** [bluejs.dev](https://bluejs.dev)

This repository contains the Blue compiler source, runtime, documentation, and examples.
