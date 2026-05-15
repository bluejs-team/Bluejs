#!/usr/bin/env node
const { execFileSync } = require("child_process");
const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "../../..");
const landing = path.resolve(__dirname, "..");
const output = path.join(landing, "bluejs_landing_v1");
const pageSource = fs.readFileSync(path.join(landing, "src/page.js"), "utf8");

for (const required of [
  "https://discord.gg/P9ym64jZqZ",
  "github.com/bluejs-team/BlueJS",
  "sudo apt install build-essential pkg-config",
  "winget",
  "MSYS2"
]) {
  if (!pageSource.includes(required)) {
    throw new Error("landing source missing required text: " + required);
  }
}

for (const stale of ["zig", "Zig", "closed source", "Closed source", "zpdkarzmd4"]) {
  if (pageSource.includes(stale)) {
    throw new Error("landing source contains stale text: " + stale);
  }
}

if (process.argv.includes("--verify-only")) {
  process.exit(0);
}

const blue = path.join(root, "blue");
execFileSync(blue, ["-build", landing, "-o", output], { stdio: "inherit" });
