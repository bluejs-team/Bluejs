# Vendored Dependencies

This directory contains third-party source and assets needed to build Blue.

## QuickJS

- Location: `vendor/quickjs`
- Version: `2025-09-13-2`
- Source URL: `https://bellard.org/quickjs/quickjs-2025-09-13-2.tar.xz`
- License: MIT, see `vendor/quickjs/LICENSE`
- Update path: change `QJS_TARBALL_VER` and `QJS_SRC_DIRNAME` in `Makefile`, then run `make deps`.

Commit QuickJS source files and license files. Do not commit generated object
files from `vendor/quickjs/obj/` or `vendor/quickjs/obj-win/`.

## JavaScript Tool Assets

- Location: `vendor/js`
- `esprima.js`: `https://cdn.jsdelivr.net/npm/esprima@4.0.1/dist/esprima.js`
- `babel.min.js`: `https://cdn.jsdelivr.net/npm/@babel/standalone@7.26.9/babel.min.js`
- Licenses: Esprima is BSD-2-Clause; Babel Standalone is MIT.

These files are downloaded by `make deps` when missing. Keep the version pins in
`Makefile` synchronized with this document.

## Header Libraries

- `src/nlohmann/json.hpp`: nlohmann/json `3.12.0`
- Source URL: `https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp`
- License: MIT

## Platform SDKs

Windows WebView2 and libuv binary SDKs may be placed under `vendor/webview2` and
`vendor/libuv-win` for local release builds. They are ignored by git.
