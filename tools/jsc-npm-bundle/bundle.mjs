#!/usr/bin/env node
// Called by blue_bin as: node bundle.mjs <island-file> <project-dir> <output-file> <platform>
import { build } from "esbuild";

const [,, islandFile, projectDir, outputFile, platform] = process.argv;

if (!islandFile || !outputFile) {
  console.error("usage: bundle.mjs <island-file> <project-dir> <output-file> <platform>");
  process.exit(1);
}

const preamble =
  "var module={exports:{}};var exports=module.exports;" +
  "if(typeof global==='undefined')global=globalThis;" +
  "if(typeof process==='undefined')globalThis.process={env:{},argv:[],cwd:function(){return'.';},nextTick:function(cb){Promise.resolve().then(cb);}};" +
  "process.env=process.env||{};" +
  "if(typeof globalThis.__dirname==='undefined')globalThis.__dirname='.';" +
  "if(typeof globalThis.__filename==='undefined')globalThis.__filename='main.js';\n";

const nodeCore = [
  "assert", "assert/strict", "async_hooks", "buffer", "child_process",
  "cluster", "console", "constants", "crypto", "dgram", "diagnostics_channel",
  "dns", "domain", "events", "fs", "fs/promises", "http", "http2", "https",
  "module", "net", "os", "path", "perf_hooks", "process", "punycode",
  "querystring", "readline", "repl", "stream", "stream/promises",
  "stream/web", "string_decoder", "timers", "timers/promises", "tls", "tty",
  "url", "util", "util/types", "v8", "vm", "worker_threads", "zlib"
];

const externalCore = new Set([
  ...nodeCore,
  ...nodeCore.map((name) => `node:${name}`),
]);

const blueNodeCompatPlugin = {
  name: "blue-node-compat",
  setup(buildApi) {
    buildApi.onResolve({ filter: /.*/ }, (args) => {
      if (externalCore.has(args.path)) {
        return { path: args.path, external: true };
      }
      if (args.path.endsWith(".node")) {
        return {
          errors: [{
            text: `Blue does not support native Node addons yet: ${args.path}`,
          }],
        };
      }
      return null;
    });
  },
};

await build({
  entryPoints: [islandFile],
  bundle: true,
  platform: platform === "node" ? "node" : "browser",
  format: "cjs",
  outfile: outputFile,
  absWorkingDir: projectDir,
  mainFields: platform === "node"
    ? ["module", "main"]
    : ["browser", "module", "main"],
  conditions: platform === "node"
    ? ["node", "require", "default"]
    : ["browser", "default"],
  external: [...externalCore],
  plugins: [blueNodeCompatPlugin],
  define: {
    "process.env.NODE_ENV": JSON.stringify(process.env.NODE_ENV || "production"),
    "process.browser": "false",
  },
  banner: { js: preamble },
  logLevel: "warning",
});
