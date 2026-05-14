"use strict";

/**
 * Minimal Blue WebView example.
 * Compile: ./blue -compile examples/hello-webview/main.js -o /tmp/hello
 * Run:     /tmp/hello
 */

var html = "<!doctype html>" +
  "<html><head>" +
  "<style>" +
  "  body { font-family: system-ui; background: #0f172a; color: #e2e8f0;" +
  "         display: flex; align-items: center; justify-content: center;" +
  "         height: 100vh; margin: 0; }" +
  "  .card { text-align: center; }" +
  "  h1 { color: #38bdf8; font-size: 2rem; margin: 0 0 0.5rem; }" +
  "  p { color: #94a3b8; font-size: 1.1rem; }" +
  "</style>" +
  "</head><body>" +
  "<div class='card'>" +
  "  <h1>Hello from Blue</h1>" +
  "  <p>This is a native window. No Electron, no browser.</p>" +
  "</div>" +
  "</body></html>";

window.open(html, "Hello Blue", 600, 400);
