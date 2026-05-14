# Contributing

Blue is a C++17 JavaScript-to-native compiler with a small shell-based build.
Keep changes scoped, buildable, and easy to audit.

## Prerequisites

Linux:

```bash
sudo apt install build-essential pkg-config curl nodejs libuv1-dev
```

Native GUI builds also need:

```bash
sudo apt install libgtk-3-dev libwebkit2gtk-4.1-dev
```

Windows cross-builds need MinGW:

```bash
sudo apt install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64
```

## Build And Test

```bash
make all
make test
make check
```

Optional checks:

```bash
make test-babel-emit
make test-aot-perf
make test-shift-left
make test-npm-bundle
make windows
```

## Style

- Use C++17.
- Prefer existing runtime helpers and CLI helpers over new abstractions.
- Keep generated files out of git.
- Put release and deployment automation under `build/`.
- Put downloaded third-party JavaScript assets under `vendor/js/`.
- Do not add em dashes. Use `-`.

Run formatting when touching C++:

```bash
make format
```

## Public Source Rules

Do not commit:

- build outputs
- runtime archives
- QuickJS object files
- `.blue-build/` or `.jsc-build/`
- `node_modules/`
- secrets or `.env` files

Do commit:

- source code
- examples
- docs
- vendored source with license and version documentation

## License Review

Blue uses the PolyForm Noncommercial License 1.0.0. Any new dependency must be
added to `THIRD_PARTY_LICENSES.md`, and any vendored dependency must include its
upstream license text or an explicit source URL.
