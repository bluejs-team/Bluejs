# Blue Documentation

See the [root README](../README.md) for an overview and quick start.

The recommended way to try Bluejs is the hosted Codespaces playground:

[Open Bluejs Playground in Codespaces](https://codespaces.new/bluejs-team/bluejs-playground)

Windows and Linux local builds are experimental. Use Codespaces first if you
want to try the examples without installing system dependencies locally.

## Reference

| Document | What it covers |
|----------|---------------|
| [Getting Started](GETTING_STARTED.md) | Install, build compiler, first app, project layout |
| [CLI Reference](CLI.md) | Commands (`-init`, `-build`, `run`, `-compile`, `--print-c`) and flags |
| [Configuration](CONFIGURATION.md) | `blue.config.json` fields and examples |
| [API Reference](API.md) | `window.open`, `Blue.Window/Dialog/Clipboard/System`, WebView bridge, Node.js shims |
| [Hybrid Mode](HYBRID.md) | Dual-entry builds, AOT ↔ Island FFI |
| [Native Plugins](PLUGINS.md) | C ABI for dynamic native plugins |
| [Strict AOT](STRICT_AOT.md) | Supported JS subset, Babel lowering, what to move to the island |
| [Troubleshooting](TROUBLESHOOTING.md) | Error messages and how to fix them |
| [Architecture](ARCHITECTURE.md) | Compiler pipeline and source layout (contributor reference) |
| [Benchmarks](BENCHMARKS.md) | Blue vs Node.js - startup time, RAM, binary size |
