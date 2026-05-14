# Blue vs Node.js - Runtime Comparison

Benchmarks run on Ubuntu Linux (Node.js v18.19.1). Blue binaries are compiled with `blue -compile` or `blue -build`. Measurements use `/usr/bin/time` for peak RSS and wall clock. Each timing test was run 3-5 times; averages are shown. Last checked: May 10, 2026.

---

## Startup time

The simplest possible program: print "hello" and exit.

| | Blue AOT | Node.js |
|---|---|---|
| Wall time (avg) | **~5 ms** | ~90 ms |
| Peak RSS | **3.8 MB** | 48.5 MB |
| User CPU time | 0.00 s | 0.08 s |
| Minor page faults | 154 | 4,132 |

Blue starts much faster and uses **12× less memory** for a trivial program. The difference is the V8 engine initialisation cost - Node.js parses and JIT-compiles before running a single line of user code.

---

## Compute - 10 million iteration loop

```js
var sum = 0;
for (var i = 0; i < 10000000; i++) { sum += i; }
console.log(sum); // 49999995000000
```

| | Blue AOT | Node.js |
|---|---|---|
| Wall time (avg) | **~52 ms** | ~104 ms |
| Peak RSS | **2.1 MB** | 52.2 MB |
| User CPU time | 0.052 s | 0.104 s |

Blue runs the loop about **2× faster** and uses **25× less memory**. The loop compiles to a native C++ `for` loop - there is no interpreter overhead. Node.js eventually JIT-compiles the hot loop too, which is why it catches up partially; the remaining gap is startup and V8 bookkeeping.

Note: Blue's AOT JS subset is limited (no `async`/`await`, no `eval`, no npm packages). For code that fits the subset, these numbers reflect the ceiling of what's possible.

---

## HTTP server - idle process footprint

An HTTP server sitting idle, no active connections. Blue's http-server example uses a QuickJS island (the island handles all HTTP logic; AOT handles native APIs). The Node.js comparison is a minimal `http.createServer` using the standard library only - no frameworks.

| | Blue (hybrid) | Node.js |
|---|---|---|
| RSS (resident memory) | **5.0 MB** | 48.8 MB |
| Virtual memory (VmSize) | **8.7 MB** | 610.4 MB |
| Threads | **2** | 10 |
| Binary size | 1.4 MB (libuv statically linked) | requires 46 MB `libnode.so` |

The Blue hybrid binary embeds QuickJS and (when `libuv.a` is present at compile time) libuv statically - the HTTP server binary above has no `libuv.so` runtime dependency. The Node.js process reserves 610 MB of virtual address space at startup (V8 heap reservation, JIT buffers, worker thread pools) even when serving zero requests.

The **2 vs 10 thread** difference is significant for resource-constrained environments. Node.js spawns a thread pool (libuv default: 4 UV threads + V8 internal threads) regardless of whether the app uses them.

---

## Distribution size

| | Blue | Node.js |
|---|---|---|
| Strict AOT CLI hello | **51 KB** | N/A - requires Node.js runtime |
| WebView hello binary | **1.2 MB** | N/A - requires Node.js runtime |
| HTTP server binary | **1.4 MB** (QuickJS + libuv statically linked) | N/A |
| `node_modules` (minimal HTTP app) | none | varies (express: ~5 MB, 57 packages) |

**What the target machine needs at runtime:**

| Binary type | Runtime requirement |
|---|---|
| AOT (no window, no HTTP) | Nothing - only `libc`, present on every Linux system |
| Hybrid / HTTP server | Nothing if compiled with static `libuv.a` (Blue prefers this automatically); otherwise `libuv1` |
| WebView (`window.open`) | `libgtk-3-0 libwebkit2gtk-4.1-0` - ~100 shared libraries |

WebView apps depend on GTK and WebKit2GTK which cannot be statically linked. For portable distribution, package WebView apps as an [AppImage](https://appimage.org) to bundle these libraries alongside the binary. AOT and hybrid HTTP binaries are fully self-contained when built on a machine with `libuv-dev` installed.

---

## What these numbers mean in practice

**Blue is faster and smaller when:**
- You need near-instant startup (CLI tools, launchers, system daemons)
- You are distributing to machines where you can't guarantee Node.js is installed
- Your logic fits the AOT JS subset (loops, classes, filesystem, native windows)
- Memory budget is constrained (embedded systems, servers running many processes)

**Node.js is the right choice when:**
- You need the full npm ecosystem
- Your code relies heavily on `async`/`await`, generators, or dynamic patterns
- V8's JIT matters for sustained throughput on complex workloads (the gap narrows under load)
- You need broad OS support without recompilation

**Hybrid Blue** (AOT + QuickJS island) sits in between: npm packages and full ES2020+ in the island, with the native binary advantages for distribution and windowed UIs. The HTTP server numbers above reflect this mode.

---

## Methodology notes

- Blue binaries were compiled with `-O2` (the default).
- Node.js was v18.19.1 (Ubuntu system package), not a custom build.
- RSS was read from `/proc/<pid>/status` (VmRSS) at steady state (1 second after start).
- Virtual memory figures (VmSize) include memory-mapped files and reserved but uncommitted pages - they do not all represent physical RAM usage.
- The `libnode.so` size (46 MB) reflects the on-disk shared library. At runtime only the pages actually touched are loaded into RAM; the 50 MB RSS figure above is what was actually resident.
- Blue's compute benchmark uses a tight integer loop - a case where Blue's C++ output is near-optimal. For floating-point heavy workloads, V8's JIT is more competitive.
