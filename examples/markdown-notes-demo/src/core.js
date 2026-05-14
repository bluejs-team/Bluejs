"use strict";

var fs = require("fs");

/**
 * Called by the WebView bridge: blue://app/__bridge__/saveNote/<encoded text>
 */
function saveNote(text) {
  fs.writeFileSync("./text.md", text || "");
  return "saved";
}

/**
 * Called by the WebView bridge: blue://app/__bridge__/loadNote/
 */
function loadNote(_payload) {
  try {
    return fs.readFileSync("./text.md");
  } catch (_e) {
    return "";
  }
}

/**
 * Returns the embedded HTML for the webview.
 * __BLUE_BUNDLE_HTML__ is replaced at compile time with public/index.html.
 */
function appHtml(_payload) {
  return __BLUE_BUNDLE_HTML__;
}

try {
  Blue.Window.setTitle("Blue Markdown Notes");
  Blue.Window.setSize(900, 650);
  Blue.Window.center();
  console.log("[Blue.System]", Blue.System.getMemoryInfo());
} catch (_e) {}

window.open(appHtml(""), "Blue Markdown Notes", 900, 650);
