# full-api-demo

Comprehensive demo of every Blue desktop API in a single native WebView window. Uses hybrid mode - an AOT core handles all native calls and a QuickJS island exposes an FFI endpoint.

## Build and run

```bash
# Build and run
blue -build examples/full-api-demo -o /tmp/full-api-demo
/tmp/full-api-demo
```

A native window opens with buttons for each API group. All output appears in the on-screen log panel.

## What it demonstrates

| API | Button |
|-----|--------|
| `Blue.System.getMemoryInfo()` / `getCPU()` | System & Memory |
| `Blue.Process.exec("uname -a")` | Process.exec |
| `Blue.Clipboard.writeText()` / `readText()` | Clipboard roundtrip |
| `Blue.Window.setTitle()` / `setSize()` / `center()` | Resize Window |
| `Blue.Dialog.showOpenDialog()` *(via bridge)* | - |
| `Blue.Tray.create()` + `Blue.GlobalShortcut.register()` | - (startup, stderr) |
| `fs.writeFileSync()` / `readFileSync()` | File System write+read |
| `crypto.createHash("sha256")` | - (startup, console) |
| `os.platform()` / `os.arch()` / `os.tmpdir()` | - (startup, console) |
| `Blue.callIsland("handlePing", ...)` | Ping Island |
| WebView bridge (`blue://app/__bridge__/fn/payload`) | all buttons |

## Architecture

```
core.js   (AOT)    ←── compiled to native C++
  │  registers bridge handler functions
  │  boots the QuickJS island
  │  calls window.open(__BLUE_BUNDLE_HTML__, ...)
  │
island.js (QuickJS) ←── full JS, bundled by esbuild
  │  exports handlePing via globalThis.__BLUE_ISLAND
  │
public/index.html   ←── embedded at compile time
       uses fetch("blue://app/__bridge__/fn/arg") to call AOT functions
```

## Notes

- `Blue.Tray` and `Blue.GlobalShortcut` print "not yet implemented" to stderr - these are stub APIs in the current Linux release.
- The HTTP server demo is a separate example: `examples/http-server`.
- See `docs/API.md` for full API reference.
