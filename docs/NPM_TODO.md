# NPM Compatibility TODO

Goal: make Blue run most pure JavaScript npm packages through the QuickJS island,
with a smaller compiled Node-compatible subset available to AOT code.

## Compatibility Targets

- Tier 1: pure JavaScript packages that use common Node core modules.
- Tier 2: packages that need HTTP, streams, crypto, Buffer, URL, timers, and fs.
- Tier 3: packages with conditional exports, ESM, or browser/node dual builds.
- Out of scope for now: native `.node` addons, worker threads, full V8 APIs, and
  exact Node event loop behavior.

## QuickJS Island

- Expand CommonJS resolution:
  - `package.json` `main`
  - `package.json` `exports`
  - `package.json` `imports`
  - directory `index.js`
  - JSON modules
  - `node:` specifiers

- Add ESM support:
  - static `import`
  - dynamic `import()`
  - default interop between CJS and ESM
  - conditional exports for `node`, `default`, `import`, and `require`

- Improve core modules:
  - `fs`
  - `fs/promises`
  - `path`
  - `buffer`
  - `events`
  - `stream`
  - `util`
  - `util/types`
  - `url`
  - `querystring`
  - `crypto`
  - `http`
  - `https`
  - `net`
  - `tls`
  - `zlib`
  - `os`
  - `process`
  - `assert`
  - `timers`
  - `timers/promises`

- Add Web APIs commonly expected by npm packages:
  - `fetch`
  - `Request`
  - `Response`
  - `Headers`
  - `AbortController`
  - `EventTarget`
  - `URL`
  - `URLSearchParams`
  - `Blob`
  - `File`
  - `FormData`
  - `TextEncoder`
  - `TextDecoder`
  - `crypto.getRandomValues`

- Improve async behavior:
  - real timer queue
  - microtask draining after native callbacks
  - unhandled rejection tracking
  - clean shutdown when handles are active
  - libuv-backed async fs and network operations

- Improve streams:
  - `Readable`
  - `Writable`
  - `Duplex`
  - `Transform`
  - backpressure
  - `pipeline`
  - `finished`
  - async iterator support

- Improve diagnostics:
  - unsupported native addon errors
  - missing core API errors
  - package resolution trace mode
  - compatibility warnings from esbuild output

## AOT Node Support

- Keep AOT scoped to deterministic Node-compatible APIs:
  - sync `fs`
  - `path`
  - `process`
  - `os`
  - `Buffer`
  - `console`
  - `assert`
  - simple `crypto`

- Add compiler recognition for:
  - `require("node:fs")`
  - destructured core imports
  - `module.exports`
  - `exports.name`
  - simple CommonJS wrappers

- Avoid broad npm execution in AOT until:
  - async functions are robust
  - promises are runtime-backed
  - callbacks can cross native boundaries safely
  - module graph lowering handles package resolution

## Test Matrix

- Add package fixtures:
  - `lodash`
  - `nanoid`
  - `uuid`
  - `debug`
  - `ms`
  - `mime-types`
  - `qs`
  - `cookie`
  - `accepts`
  - `parseurl`
  - `finalhandler`
  - `serve-static`
  - `express`
  - `koa`
  - `fastify`
  - `commander`
  - `chalk`
  - `kleur`
  - `semver`
  - `glob`
  - `minimatch`
  - `zod`
  - `date-fns`
  - `marked`

- For each fixture:
  - install with npm
  - bundle with Blue
  - run in QuickJS island
  - compare output with Node.js
  - record missing APIs

- Add compatibility reports:
  - package name
  - package version
  - pass/fail
  - missing module
  - missing method
  - runtime exception
  - bundle size
  - startup time

## Release Criteria

- `make check` passes.
- `make test-qjs-node-compat` passes.
- At least 50 pure-JS npm fixture packages pass.
- No generated artifacts are tracked.
- No em dash codepoints exist in project text.
- `THIRD_PARTY_LICENSES.md` lists every added dependency.

## Known Non-Goals

- Full Node binary addon support.
- Full N-API support.
- V8-specific APIs.
- Exact worker thread semantics.
- Exact stream internals for every package.

