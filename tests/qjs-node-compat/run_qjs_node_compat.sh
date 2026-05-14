#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_ROOT="${TMPDIR:-/tmp}/blue_qjs_node_compat"

rm -rf "${TMP_ROOT}"
mkdir -p "${TMP_ROOT}/src" "${TMP_ROOT}/public" "${TMP_ROOT}/node_modules/blue-fixture"

cat > "${TMP_ROOT}/blue.config.json" <<'JSON'
{
  "entry": "src/main.js",
  "quickjsIsland": "src/island.js",
  "hybrid": true,
  "bundlePlatform": "node",
  "publicDir": "public",
  "bundleDependencies": false
}
JSON

cat > "${TMP_ROOT}/node_modules/blue-fixture/package.json" <<'JSON'
{
  "name": "blue-fixture",
  "version": "1.0.0",
  "main": "./index.js",
  "exports": {
    ".": "./index.js",
    "./feature": "./feature.js"
  }
}
JSON

cat > "${TMP_ROOT}/node_modules/blue-fixture/index.js" <<'JS'
"use strict";

const path = require("node:path");
const { EventEmitter } = require("events");
const { performance } = require("perf_hooks");
const { builtinModules, isBuiltin } = require("module");

exports.run = function run() {
  const ee = new EventEmitter();
  let value = "";
  ee.once("data", v => {
    value = v;
  });
  ee.emit("data", path.basename("/tmp/blue.txt", ".txt"));
  return {
    value,
    now: typeof performance.now() === "number",
    hasFs: builtinModules.indexOf("fs") >= 0 && isBuiltin("node:path")
  };
};
JS

cat > "${TMP_ROOT}/node_modules/blue-fixture/feature.js" <<'JS'
"use strict";

const assert = require("assert/strict");
const utilTypes = require("util/types");

module.exports = function feature(input) {
  const buf = Buffer.from(String(input));
  assert.equal(buf.toString("hex"), "6f6b");
  return utilTypes.isTypedArray(buf);
};
JS

cat > "${TMP_ROOT}/src/main.js" <<'JS'
"use strict";
console.log("aot-main");
JS

cat > "${TMP_ROOT}/src/island.js" <<'JS'
"use strict";

const assert = require("node:assert");
const path = require("node:path");
const qs = require("querystring");
const url = require("url");
const { PassThrough } = require("stream");
const timers = require("timers/promises");
const fsp = require("fs/promises");
const fixture = require("blue-fixture");
const feature = require("blue-fixture/feature");

assert.strictEqual(path.join("/a", "b", "..", "c"), "/a/c");
assert.deepStrictEqual(qs.parse("a=1&b=two"), { a: "1", b: "two" });
assert.strictEqual(url.parse("https://x.test/a?b=1", true).query.b, "1");
assert.strictEqual(Buffer.from("6869", "hex").toString(), "hi");
assert.strictEqual(Buffer.from("hi").toString("base64"), "aGk=");
assert.deepStrictEqual(fixture.run(), { value: "blue", now: true, hasFs: true });
assert.strictEqual(feature("ok"), true);

const pt = new PassThrough();
let seen = "";
pt.on("data", c => {
  seen += Buffer.from(c).toString();
});
pt.write(Buffer.from("ok"));
assert.strictEqual(seen, "ok");

fsp.writeFile("/tmp/blue_qjs_node_compat_out.txt", "done")
  .then(() => fsp.readFile("/tmp/blue_qjs_node_compat_out.txt", "utf8"))
  .then(v => {
    assert.strictEqual(String(v), "done");
    return timers.setTimeout(0, "timer-ok");
  })
  .then(v => {
    assert.strictEqual(v, "timer-ok");
    console.log("compat-ok");
  })
  .catch(e => {
    console.error(e && e.stack || e);
    process.exit(1);
  });
JS

cd "${ROOT}"
bash build/precompile-runtime.sh >/dev/null
./blue -build "${TMP_ROOT}" -o "${TMP_ROOT}/app" >/tmp/blue_qjs_node_compat_build.log 2>&1
timeout 5s "${TMP_ROOT}/app" >"${TMP_ROOT}/out.txt" 2>&1
grep -q "compat-ok" "${TMP_ROOT}/out.txt"
