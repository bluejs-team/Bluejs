"use strict";

var fs     = require('fs');
var path   = require('path');
var os     = require('os');
var crypto = require('crypto');

console.log("[Core] Blue full-api-demo starting");
console.log("[Core] Platform:", os.platform(), "| Arch:", os.arch());

// SHA-256 at startup to exercise crypto
var h = crypto.createHash("sha256");
h.update("Blue is awesome");
console.log("[Core] SHA-256:", h.digest("hex"));

// ── Bridge handlers - called via blue://app/__bridge__/<name>/<payload> ─────

function calculatePrimes(limit) {
  var lim = Number(limit) || 100;
  var out = "";
  for (var i = 2; i <= lim; i++) {
    var ok = true;
    for (var j = 2; j * j <= i; j++) {
      if (i % j === 0) { ok = false; break; }
    }
    if (ok) { out = out + i + " "; }
  }
  return out;
}

function getSystemStatus() {
  var mem = Blue.System.getMemoryInfo();
  var cpu = Blue.System.getCPU();
  var memStr = mem.supported
    ? Math.floor(mem.memAvailableKb / 1024) + " MB free"
    : "unavailable";
  var cpuStr = cpu.supported ? "supported" : "unavailable";
  return "Memory: " + memStr + " | CPU: " + cpuStr;
}

function testFileSystem() {
  var logPath = path.join(os.tmpdir(), "blue-demo.log");
  fs.writeFileSync(logPath, "blue-test-entry\n");
  var back = fs.readFileSync(logPath);
  return "Wrote + read: " + back;
}

function testExec() {
  var cmd = os.platform() === "win32" ? "ver" : "uname -a";
  var res = Blue.Process.exec(cmd);
  if (res.code === 0) {
    return res.stdout;
  }
  return "exit " + res.code;
}

function testClipboard(text) {
  var s = text || "Hello from Blue!";
  Blue.Clipboard.writeText(s);
  return Blue.Clipboard.readText();
}

function testWindowControl() {
  Blue.Window.setTitle("Blue Full API Demo - Resized");
  Blue.Window.setSize(1100, 750);
  Blue.Window.center();
  return "window updated";
}

function pingIsland() {
  var res = Blue.callIsland("handlePing", "{\"msg\":\"Hello from Core!\"}");
  return res;
}

// ── Open the WebView with the embedded HTML ───────────────────────────────────
Blue.Window.setTitle("Blue Full API Demo");
Blue.Window.setSize(960, 700);
Blue.Window.center();
window.open(__BLUE_BUNDLE_HTML__, "Blue Full API Demo", 960, 700);
