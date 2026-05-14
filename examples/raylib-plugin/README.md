# Raylib Plugin Example

This example shows how to build a Blue native plugin in C and call it from the
QuickJS island.

The plugin wraps a small raylib surface:

- `Raylib.initWindow(width, height, title)`
- `Raylib.windowShouldClose()`
- `Raylib.beginDrawing()`
- `Raylib.clearBackground(r, g, b)`
- `Raylib.drawText(text, x, y, size, r, g, b)`
- `Raylib.endDrawing()`
- `Raylib.closeWindow()`

## Requirements

Linux:

```bash
sudo apt install build-essential pkg-config
```

The example can install raylib locally under `examples/raylib-plugin/.raylib`:

```bash
make -C examples/raylib-plugin install-raylib
```

You can also use a system raylib install. The Makefile checks local raylib first,
then falls back to `pkg-config raylib`.

## Build And Run

From the repo root:

```bash
make -C examples/raylib-plugin install-raylib all
./blue -build examples/raylib-plugin -o /tmp/blue_raylib_demo
/tmp/blue_raylib_demo
```

The plugin is linked with an rpath for the local raylib install. If you override
`RAYLIB_PREFIX`, set `LD_LIBRARY_PATH` to that prefix's `lib` directory before
running the compiled demo.
