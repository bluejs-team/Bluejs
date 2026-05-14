# Blue Examples

Each folder is a complete, runnable project. The easiest way to try them is the
hosted playground:

[Open Bluejs Playground in Codespaces](https://codespaces.new/bluejs-team/bluejs-playground)

Codespaces is recommended for first-run testing. Local Windows and Linux builds
are experimental.

| Directory | What it demonstrates |
|-----------|---------------------|
| **[hello-webview](hello-webview/)** | Open a native window in 25 lines. No dependencies. |
| **[aot-math](aot-math/)** | AOT-compiled loops - the simplest possible build. |
| **[markdown-notes-demo](markdown-notes-demo/)** | WebView + filesystem + `blue://` bridge. No HTTP. |
| **[http-server](http-server/)** | Hybrid: QuickJS island runs an HTTP server, AOT provides `Blue.System`. |
| **[react-init-hybrid](react-init-hybrid/)** | Hybrid + React: island serves a React UI with AOT ↔ island FFI. |
| **[raylib-plugin](raylib-plugin/)** | Native C plugin API with a raylib-powered window. |

## Quick start

```bash
blue --version
```

Then build any example:

```bash
# AOT only (no config needed)
blue -compile examples/aot-math/main.js -o /tmp/math
/tmp/math

# Project builds (uses blue.config.json)
blue -build examples/http-server -o /tmp/http
/tmp/http
```

React is a build-time dependency for `react-init-hybrid`:

```bash
cd examples/react-init-hybrid
npm install
cd ../..

blue -build examples/react-init-hybrid -o /tmp/react
/tmp/react
```

WebView desktop examples compile in Codespaces but need a real desktop session
to run interactively:

```bash
blue -compile examples/hello-webview/main.js -o /tmp/hello
blue -build examples/markdown-notes-demo -o /tmp/notes
blue -build examples/full-api-demo -o /tmp/api_demo
```
