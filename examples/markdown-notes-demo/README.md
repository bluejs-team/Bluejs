# markdown-notes-demo

A native markdown notes app built with Blue. Opens a WebView window, edits text, and saves to `./text.md` - all through the `blue://` bridge. No HTTP server, no npm dependencies.

## Build and run

```bash
blue -build examples/markdown-notes-demo -o /tmp/notes
/tmp/notes
```

A native window opens with a text editor. Saves write to `./text.md` in the working directory.

## How it works

- **`src/core.js`** - AOT entry. Defines `saveNote` and `loadNote` functions, then opens a native window with `window.open`.
- **`public/index.html`** - the UI. Vanilla HTML/CSS/JS. Saves and loads via `blue://app/__bridge__/saveNote/...` and `blue://app/__bridge__/loadNote/`.
- No island, no HTTP server, no React, no npm.

## What it demonstrates

- `window.open` with embedded HTML (`__BLUE_BUNDLE_HTML__`)
- `blue://app/__bridge__/` for WebView ↔ AOT communication
- `fs.writeFileSync` / `fs.readFileSync` from AOT
- `Blue.Window.setTitle`, `Blue.Window.setSize`, `Blue.Window.center`
- `Blue.System.getMemoryInfo`
