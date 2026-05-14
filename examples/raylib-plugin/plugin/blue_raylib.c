#include "runtime/blue_plugin.h"

#include <raylib.h>

static const BluePluginHost* g_host;

static const char* str_arg(int argc, const BluePluginValue* argv, int index,
                           const char* fallback) {
    if (index >= argc || argv[index].type != BLUE_PLUGIN_STRING || !argv[index].string)
        return fallback;
    return argv[index].string;
}

static int int_arg(int argc, const BluePluginValue* argv, int index, int fallback) {
    if (index >= argc || argv[index].type != BLUE_PLUGIN_NUMBER)
        return fallback;
    return (int)argv[index].number;
}

static BluePluginValue rl_init_window(BluePluginCallContext* ctx, int argc,
                                      const BluePluginValue* argv) {
    (void)ctx;
    int width = int_arg(argc, argv, 0, 800);
    int height = int_arg(argc, argv, 1, 450);
    const char* title = str_arg(argc, argv, 2, "Blue raylib");
    InitWindow(width, height, title);
    SetTargetFPS(60);
    return g_host->make_undefined();
}

static BluePluginValue rl_window_should_close(BluePluginCallContext* ctx, int argc,
                                              const BluePluginValue* argv) {
    (void)ctx;
    (void)argc;
    (void)argv;
    return g_host->make_bool(WindowShouldClose());
}

static BluePluginValue rl_begin_drawing(BluePluginCallContext* ctx, int argc,
                                        const BluePluginValue* argv) {
    (void)ctx;
    (void)argc;
    (void)argv;
    BeginDrawing();
    return g_host->make_undefined();
}

static BluePluginValue rl_clear_background(BluePluginCallContext* ctx, int argc,
                                           const BluePluginValue* argv) {
    (void)ctx;
    Color c = {
        (unsigned char)int_arg(argc, argv, 0, 0),
        (unsigned char)int_arg(argc, argv, 1, 0),
        (unsigned char)int_arg(argc, argv, 2, 0),
        255
    };
    ClearBackground(c);
    return g_host->make_undefined();
}

static BluePluginValue rl_draw_text(BluePluginCallContext* ctx, int argc,
                                    const BluePluginValue* argv) {
    (void)ctx;
    const char* text = str_arg(argc, argv, 0, "");
    int x = int_arg(argc, argv, 1, 0);
    int y = int_arg(argc, argv, 2, 0);
    int size = int_arg(argc, argv, 3, 20);
    Color c = {
        (unsigned char)int_arg(argc, argv, 4, 255),
        (unsigned char)int_arg(argc, argv, 5, 255),
        (unsigned char)int_arg(argc, argv, 6, 255),
        255
    };
    DrawText(text, x, y, size, c);
    return g_host->make_undefined();
}

static BluePluginValue rl_end_drawing(BluePluginCallContext* ctx, int argc,
                                      const BluePluginValue* argv) {
    (void)ctx;
    (void)argc;
    (void)argv;
    EndDrawing();
    return g_host->make_undefined();
}

static BluePluginValue rl_close_window(BluePluginCallContext* ctx, int argc,
                                       const BluePluginValue* argv) {
    (void)ctx;
    (void)argc;
    (void)argv;
    CloseWindow();
    return g_host->make_undefined();
}

BLUE_PLUGIN_EXPORT int blue_plugin_init(const BluePluginHost* host) {
    if (!host || host->api_version != BLUE_PLUGIN_API_VERSION)
        return 0;
    g_host = host;
    host->define_function("Raylib", "initWindow", rl_init_window, 3, 0);
    host->define_function("Raylib", "windowShouldClose", rl_window_should_close, 0, 0);
    host->define_function("Raylib", "beginDrawing", rl_begin_drawing, 0, 0);
    host->define_function("Raylib", "clearBackground", rl_clear_background, 3, 0);
    host->define_function("Raylib", "drawText", rl_draw_text, 7, 0);
    host->define_function("Raylib", "endDrawing", rl_end_drawing, 0, 0);
    host->define_function("Raylib", "closeWindow", rl_close_window, 0, 0);
    return 1;
}
