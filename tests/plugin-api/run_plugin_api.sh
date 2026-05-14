#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_ROOT="${TMPDIR:-/tmp}/blue_plugin_api_test"

rm -rf "${TMP_ROOT}"
mkdir -p "${TMP_ROOT}/src" "${TMP_ROOT}/public" "${TMP_ROOT}/plugin"

cat > "${TMP_ROOT}/plugin/test_plugin.c" <<'C'
#include "runtime/blue_plugin.h"

static const BluePluginHost* g_host;

static BluePluginValue add_numbers(BluePluginCallContext* ctx, int argc,
                                   const BluePluginValue* argv) {
    (void)ctx;
    double a = argc > 0 && argv[0].type == BLUE_PLUGIN_NUMBER ? argv[0].number : 0.0;
    double b = argc > 1 && argv[1].type == BLUE_PLUGIN_NUMBER ? argv[1].number : 0.0;
    return g_host->make_number(a + b);
}

static BluePluginValue hello(BluePluginCallContext* ctx, int argc,
                             const BluePluginValue* argv) {
    (void)ctx;
    (void)argc;
    (void)argv;
    return g_host->make_string("hello from plugin");
}

BLUE_PLUGIN_EXPORT int blue_plugin_init(const BluePluginHost* host) {
    if (!host || host->api_version != BLUE_PLUGIN_API_VERSION)
        return 0;
    g_host = host;
    host->define_function("BlueTestPlugin", "add", add_numbers, 2, 0);
    host->define_function("BlueTestPlugin", "hello", hello, 0, 0);
    return 1;
}
C

cc -shared -fPIC -I"${ROOT}/src" \
  "${TMP_ROOT}/plugin/test_plugin.c" \
  -o "${TMP_ROOT}/plugin/libblue_test_plugin.so"

cat > "${TMP_ROOT}/aot-main.js" <<JS
"use strict";
if (!Blue.Plugin.load("${TMP_ROOT}/plugin/libblue_test_plugin.so")) {
  console.log("aot-plugin-load-failed");
}
console.log("aot-plugin", globalThis.BlueTestPlugin.add(20, 22), globalThis.BlueTestPlugin.hello());
JS

cat > "${TMP_ROOT}/blue.config.json" <<JSON
{
  "entry": "src/main.js",
  "quickjsIsland": "src/island.js",
  "hybrid": true,
  "bundlePlatform": "node",
  "publicDir": "public",
  "bundleDependencies": false
}
JSON

cat > "${TMP_ROOT}/src/main.js" <<'JS'
"use strict";
console.log("aot-main");
JS

cat > "${TMP_ROOT}/src/island.js" <<JS
"use strict";
const assert = require("assert");
if (!Blue.Plugin.load("${TMP_ROOT}/plugin/libblue_test_plugin.so")) {
  throw new Error("plugin load failed");
}
assert.strictEqual(BlueTestPlugin.add(20, 22), 42);
assert.strictEqual(BlueTestPlugin.hello(), "hello from plugin");
console.log("plugin-ok");
JS

cd "${ROOT}"
bash build/precompile-runtime.sh >/dev/null
./blue -compile "${TMP_ROOT}/aot-main.js" -o "${TMP_ROOT}/aot-app" >/tmp/blue_plugin_api_aot_build.log 2>&1
timeout 5s "${TMP_ROOT}/aot-app" >"${TMP_ROOT}/aot-out.txt" 2>&1
grep -q "aot-plugin 42 hello from plugin" "${TMP_ROOT}/aot-out.txt"
./blue -build "${TMP_ROOT}" -o "${TMP_ROOT}/hybrid-app" >/tmp/blue_plugin_api_hybrid_build.log 2>&1
timeout 5s "${TMP_ROOT}/hybrid-app" >"${TMP_ROOT}/hybrid-out.txt" 2>&1
grep -q "plugin-ok" "${TMP_ROOT}/hybrid-out.txt"
