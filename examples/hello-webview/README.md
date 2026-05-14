# hello-webview

The simplest possible native GUI with Blue. Opens a window with inline HTML - no npm, no island, no React.

## Build and run

```bash
blue -compile examples/hello-webview/main.js -o /tmp/hello
/tmp/hello
```

## What it demonstrates

- `window.open(html, title, width, height)` - opens a native GTK/WebKit window
- Inline HTML with embedded CSS
- Single-file AOT compilation (no `blue.config.json` needed)
