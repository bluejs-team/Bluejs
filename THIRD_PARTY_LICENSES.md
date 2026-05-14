# Third-Party Licenses

This file lists the main third-party source, tools, and runtime assets used by
Blue. It is an inventory for release review, not a replacement for the license
files shipped by each dependency.

## Project License

- Blue source: PolyForm Small Business License 1.0.0
- SPDX identifier: `PolyForm-Small-Business-1.0.0`
- License file: `LICENSE`
- Required notice file: `NOTICE`

## Vendored Source

| Component | Version | Location | License |
|---|---:|---|---|
| QuickJS | 2025-09-13-2 | `vendor/quickjs` | MIT |
| nlohmann/json | 3.12.0 | `src/nlohmann/json.hpp` | MIT |

QuickJS includes its upstream license at `vendor/quickjs/LICENSE`.
nlohmann/json is a single-header dependency fetched from the upstream release.

## Vendored JavaScript Assets

| Component | Version | Location | License |
|---|---:|---|---|
| Esprima | 4.0.1 | `vendor/js/esprima.js` | BSD-2-Clause |
| Babel Standalone | 7.26.9 | `vendor/js/babel.min.js` | MIT |

These assets are downloaded by `make deps` when missing.

## Node Tooling

The npm bundling helper under `tools/jsc-npm-bundle` depends on esbuild.
The package lock records esbuild and platform packages as MIT licensed.

| Component | Version | Location | License |
|---|---:|---|---|
| esbuild | 0.25.12 | `tools/jsc-npm-bundle/package-lock.json` | MIT |
| @esbuild platform packages | 0.25.12 | `tools/jsc-npm-bundle/package-lock.json` | MIT |

The React example manifests include these runtime dependencies:

| Component | Version | Location | License |
|---|---:|---|---|
| React | 18.3.1 | `examples/react-init-hybrid/package-lock.json` | MIT |
| React DOM | 18.3.1 | `examples/react-init-hybrid/package-lock.json` | MIT |
| scheduler | 0.23.2 | `examples/react-init-hybrid/package-lock.json` | MIT |
| loose-envify | 1.4.0 | `examples/react-init-hybrid/package-lock.json` | MIT |
| js-tokens | 4.0.0 | `examples/react-init-hybrid/package-lock.json` | MIT |

The same React example dependency set is mirrored under
`bluejs-playground/examples/react-init-hybrid`.

## External Build And Runtime Tools

These tools are not vendored as source in this repository, but are used by the
build, packaging, or generated applications.

| Tool | Used For | License To Verify Upstream |
|---|---|---|
| GCC / g++ | C++ compilation | GPL-3.0-or-later with GCC Runtime Library Exception |
| MinGW-w64 | Windows cross-compilation | Mixed permissive and copyleft components |
| curl | Dependency download and smoke tests | curl license |
| tar | Dependency extraction | GPL-3.0-or-later |
| Node.js | npm bundling helper | MIT |
| npm | Installing JavaScript tool dependencies | Artistic-2.0 |
| pkg-config | Native dependency discovery | GPL-2.0-or-later |
| libuv | Hybrid island and HTTP server runtime support | MIT |
| OpenSSL | Optional crypto support | Apache-2.0 |
| GTK 3 | Linux native GUI support | LGPL-2.1-or-later |
| WebKitGTK | Linux WebView support | LGPL-2.1-or-later / BSD-style components |
| WebView2 SDK / Runtime | Windows WebView support | Microsoft license terms |
| raylib | Optional `examples/raylib-plugin` native plugin | zlib/libpng |
| NSIS | Windows installer generation | zlib/libpng |
| Wine | Windows smoke tests on Linux | LGPL-2.1-or-later |
| sshpass / OpenSSH / rsync | Maintainer deploy script | GPL-2.0-or-later / BSD-style / GPL-3.0-or-later |

Before shipping binary distributions, verify the exact installed versions and
their license texts for the target release environment.
