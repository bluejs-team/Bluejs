# API Reference

## Native window

### `window.open(html, title, width, height)`

Opens a native GTK/WebKit window with the given HTML content. Blocks until the window is closed.

```js
window.open("<h1>Hello</h1>", "My App", 800, 600);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `html` | string | HTML content to display |
| `title` | string | Window title |
| `width` | number | Window width in pixels |
| `height` | number | Window height in pixels |

The HTML is loaded under the `blue://app/` custom URI scheme. Assets from `public/` are served via `blue://app/filename`. Use `__BLUE_BUNDLE_HTML__` in your entry file to embed `public/index.html` at compile time.

---

## Blue.Window

Control the native window from JavaScript. Available in both AOT and Island contexts.

```js
Blue.Window.setTitle("New Title");
Blue.Window.setSize(1024, 768);
Blue.Window.center();
```

| Method | Description |
|--------|-------------|
| `setTitle(title)` | Set the window title bar text |
| `setSize(w, h)` | Resize the window |
| `center()` | Center the window on screen |
| `setFrameless(bool)` | Remove/restore window decorations |
| `minimize()` | Minimize the window |
| `maximize()` | Maximize the window |
| `close()` | Close and destroy the window |
| `setAlwaysOnTop(bool)` | Keep window above others |

---

## Blue.Dialog

Native file and message dialogs. These block until the user responds.

### `Blue.Dialog.showOpenDialog(title, patterns, multiple)`

```js
var files = Blue.Dialog.showOpenDialog("Open File", "*.md,*.txt", false);
// returns: "/path/to/file.md" or "" if cancelled
// with multiple=true: paths separated by "|"
```

### `Blue.Dialog.showSaveDialog(title, defaultName)`

```js
var path = Blue.Dialog.showSaveDialog("Save Note", "untitled.md");
```

### `Blue.Dialog.showMessageBox(title, message, type, buttons)`

```js
var idx = Blue.Dialog.showMessageBox("Confirm", "Delete?", "question", "Yes\nNo");
// returns: button index (0, 1, ...) or -1 if dismissed
// type: "info", "warning", "error", "question"
```

---

## Blue.Clipboard

```js
Blue.Clipboard.writeText("copied text");
var text = Blue.Clipboard.readText();
```

| Method | Description |
|--------|-------------|
| `writeText(text)` | Copy text to system clipboard |
| `readText()` | Read text from system clipboard |

---

## Blue.Process

```js
var result = Blue.Process.exec("ls -la");
// result: { code: 0, stdout: "...", stderr: "" }
```

Runs a shell command synchronously. Returns an object with `code`, `stdout`, and `stderr`.

---

## Blue.System

```js
var mem = Blue.System.getMemoryInfo();
var cpu = Blue.System.getCPU();
```

| Method | Returns |
|--------|---------|
| `getMemoryInfo()` | JSON string: `{"supported":true, "memTotalKb":..., "memAvailableKb":..., "memFreeKb":...}` |
| `getCPU()` | JSON string: `{"supported":true, ...}` |

On unsupported platforms, returns `{"supported":false}`.

---

## WebView bridge (`blue://`)

When HTML is loaded in a native window, it runs under the `blue://app/` scheme. You can call AOT functions directly from the frontend:

```js
// From frontend JavaScript (inside the WebView):
fetch("blue://app/__bridge__/saveNote/" + encodeURIComponent(text))
  .then(r => r.text())
  .then(result => console.log(result)); // "saved"
```

### URL format

```
blue://app/__bridge__/<functionName>/<url-encoded-argument>
```

The bridge calls the named AOT function with the decoded argument string and returns the result as `text/plain`.

### Bridge events

The WebView dispatches a `CustomEvent` named `blue-backend` on `window` after bridge responses:

```js
window.addEventListener("blue-backend", function(ev) {
  console.log(ev.detail);
});
```

### Embedded assets

Files in `public/` are accessible as:

```html
<img src="blue://app/logo.png" />
<link rel="stylesheet" href="blue://app/style.css" />
```

---

## Hybrid FFI

### Island → AOT: `Blue.callAot(name, payload)`

From QuickJS island code, call a function defined in the AOT entry:

```js
// island.js
var result = Blue.callAot("saveNote", "# My note");
```

The function name must match a top-level function in your AOT entry file.

### AOT → Island: `Blue.callIsland(name, payload)`

From AOT entry code, call an export defined in the island:

```js
// main.js (AOT)
var result = Blue.callIsland("processData", jsonString);
```

The island must expose the function on `Blue.island`:

```js
// island.js
Blue.island.processData = function(payload) {
  return JSON.stringify({ processed: true });
};
```

---

## Native plugins

QuickJS island code can load native C plugins:

```js
Blue.Plugin.load("./my_plugin.so");
MyPlugin.someFunction(123);
```

Plugins use the C ABI in `src/runtime/blue_plugin.h`. See
[Native Plugins](PLUGINS.md) and [examples/raylib-plugin](../examples/raylib-plugin/).

---

## Node.js shims

The AOT runtime provides shims for common Node.js built-in modules:

### `fs`

```js
var fs = require("fs");
fs.readFileSync(path)              // returns string or undefined
fs.writeFileSync(path, data)       // writes string to file
fs.existsSync(path)                // returns boolean
```

### `path`

```js
var path = require("path");
path.join(a, b, ...)    // join segments
path.resolve(p)          // resolve to absolute path
path.dirname(p)          // directory part
path.basename(p, ext)    // filename part
path.extname(p)          // extension with dot
```

### `process`

```js
process.cwd()            // current working directory
process.argv             // command-line arguments (object with numeric keys + length)
process.env.KEY          // environment variable (read-only snapshot)
process.exit(code)       // exit the process
process.pid              // process ID
process.platform         // "linux" or "win32"
```

### `os`

```js
var os = require("os");
os.platform()   // "linux" or "win32"
os.homedir()    // home directory
os.tmpdir()     // temp directory
```

### `http` (Island only)

```js
var http = require("http");
http.createServer(function(req, res) {
  res.statusCode = 200;
  res.setHeader("content-type", "text/plain");
  res.end("hello");
}).listen(3000, "127.0.0.1");
```

HTTP is available in the QuickJS island via libuv. It provides `IncomingMessage` and `ServerResponse` objects with enough compatibility for Express-style middleware.

---

## Platform support

Linux and Windows local builds are experimental. Native GUI APIs (`Blue.Window`, `Blue.Dialog`, `Blue.Clipboard`, `Blue.Process`, `Blue.System`) are implemented on both platforms. Windows WebView apps use the bundled WebView2 SDK at build time and require the Microsoft Edge WebView2 Runtime on the target machine.

| Feature | Linux | Windows |
|---------|-------|---------|
| AOT compilation | ✅ | ✅ |
| `window.open` | WebKit2GTK | WebView2 SDK bundled; requires WebView2 Runtime |
| `Blue.Window.*` | GTK | ✅ Win32 |
| `Blue.Dialog.*` | GTK | ✅ Win32 (`OPENFILENAMEW`, `TaskDialogIndirect`) |
| `Blue.Clipboard` | GTK | ✅ Win32 |
| `Blue.Process.exec` | ✅ | ✅ (`CreateProcessW`) |
| `Blue.System` | ✅ | ✅ (`GlobalMemoryStatusEx`, `GetSystemTimes`) |
| HTTP (island) | libuv | libuv |
