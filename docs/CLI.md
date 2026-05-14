# CLI Reference

## Commands

### `blue -init [<dir>]`

Scaffold a new project with `blue.config.json`, sample source files, and `public/`.
Defaults to the current directory.

```bash
blue -init myapp
blue -init myapp --build   # scaffold and build immediately
```

| Flag | Description |
|------|-------------|
| `--build` | Build the project immediately after scaffolding |

---

### `blue -build [<dir>]`

Build a project described by `blue.config.json`. Defaults to the current directory.

```bash
blue -build myapp -o /tmp/myapp
blue -build .               # build current directory
```

| Flag | Description |
|------|-------------|
| `-o <path>` | Output binary path. Defaults to `<dir>/.blue-build/<name>_app.bin` |
| `--no-inline-html` | Skip embedding `public/index.html` into the binary |
| `--time` / `--benchmark` | Print build time in milliseconds after completion |

---

### `blue run [<dir>]`

Build a project and immediately execute the resulting binary.

```bash
blue run myapp
blue run myapp --time        # print build + start time
blue run myapp -- --port 8080  # pass args to the binary
```

| Flag | Description |
|------|-------------|
| `--time` / `--benchmark` | Print build time and process start time |
| `-- <args...>` | Arguments forwarded to the binary |

---

### `blue -compile <file>`

Compile a single JavaScript file to a native executable.

```bash
blue -compile app.js -o myapp
blue -compile app.js -o myapp -cflags "-O3"
```

| Flag | Description |
|------|-------------|
| `-o <path>` | Output binary path. Defaults to `<stem>.out` |
| `-cflags "..."` | Extra flags passed to the C++ compiler |

---

### `blue --print-c <file>`

Emit the generated C++ to stdout without compiling. The primary debugging tool - use it to see exactly what the emitter produced for any JS input.

```bash
blue --print-c app.js | less
blue --print-c app.js | grep "my_function"
```

---

### `blue --version`

Print the compiler version and exit.

### `blue --help`

Print usage and exit.

---

## Environment variables

| Variable | Effect |
|----------|--------|
| `CXX` | C++ compiler to use (default: `c++` on Linux, `g++` on Windows) |
| `BLUE_BIN` | Path to the `blue_bin` executable used by the launcher wrapper |
| `BLUE_NODE` | Path to the `node` binary for npm bundling |
| `BLUE_STRICT_UNSUPPORTED` | Set to `1` to turn unimplemented JS feature warnings into hard errors |
| `LIBUV_PATH` | Path to a libuv installation when pkg-config cannot find it (Windows) |
| `OPENSSL_PATH` | Path to an OpenSSL installation when pkg-config cannot find it (Windows) |
| `WEBVIEW2_SDK_PATH` | Optional override for the bundled Microsoft WebView2 SDK (Windows only) |
