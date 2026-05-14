/*
 * Minimal libuv-backed timers for JsValue(JS_FUNCTION) callbacks when libuv exists.
 */

#if defined(JSC_HAVE_UV)
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <vector>

#include "../js_value.h"
#include <uv.h>

#if !defined(JSC_RUNTIME_QUICKJS)
extern "C" void jsc_quickjs_pump_microtasks(void) {}
#else
extern "C" void jsc_quickjs_pump_microtasks(void);
#endif

struct JscUvTimerWrap {
    uv_timer_t timer{};
    JsValue    cb{};
};

namespace {

uv_loop_t* jsc_uv_loop(void) { return uv_default_loop(); }

std::vector<JscUvTimerWrap*>& all_timers(void) {
    static std::vector<JscUvTimerWrap*> g;
    return g;
}

JscUvTimerWrap* uvwrap_from_handle(uv_timer_t* h) {
    return reinterpret_cast<JscUvTimerWrap*>(
        reinterpret_cast<char*>(h) -
        offsetof(JscUvTimerWrap, timer));
}

void once_cb(uv_timer_t* h) {
    JscUvTimerWrap* w = uvwrap_from_handle(h);
    if (w && w->cb.tag == JS_FUNCTION && w->cb.as.func)
        (void)js_call_argv(w->cb, 0, nullptr);
    jsc_quickjs_pump_microtasks();
    uv_timer_stop(h);
}

void repeat_cb(uv_timer_t* h) {
    JscUvTimerWrap* w = uvwrap_from_handle(h);
    if (w && w->cb.tag == JS_FUNCTION && w->cb.as.func)
        (void)js_call_argv(w->cb, 0, nullptr);
    jsc_quickjs_pump_microtasks();
}

} /* namespace */

extern "C" void jsc_uv_set_timeout_ms(JsValue callback, JsValue delay_ms) {
    if (callback.tag != JS_FUNCTION || !callback.as.func)
        return;
    int ms = static_cast<int>(js_to_num(delay_ms));
    if (ms < 0)
        ms = 0;

    auto* w = static_cast<JscUvTimerWrap*>(std::calloc(1, sizeof(JscUvTimerWrap)));
    if (!w)
        return;
    w->cb = callback;

    uv_timer_init(jsc_uv_loop(), &w->timer);
    w->timer.data = w;

    uv_timer_start(&w->timer, once_cb,
                   ms == 0 ? 1u : static_cast<unsigned>(ms), 0u);
    all_timers().push_back(w);
}

extern "C" void jsc_uv_set_interval_ms(JsValue callback, JsValue delay_ms) {
    if (callback.tag != JS_FUNCTION || !callback.as.func)
        return;
    int ms = static_cast<int>(js_to_num(delay_ms));
    if (ms < 1)
        ms = 1;

    auto* w = static_cast<JscUvTimerWrap*>(std::calloc(1, sizeof(JscUvTimerWrap)));
    if (!w)
        return;
    w->cb = callback;

    uv_timer_init(jsc_uv_loop(), &w->timer);
    w->timer.data = w;
    uv_timer_start(&w->timer, repeat_cb, static_cast<unsigned>(ms),
                   static_cast<unsigned>(ms));

    all_timers().push_back(w);
}

extern "C" void jsc_uv_clear_timer(JsValue) {
    /* Optional: iterate all_timers matching id when timer handles are plumbed through. */
}

extern "C" void jsc_uv_run_loop_until_idle(void) {
    uv_loop_t* loop = jsc_uv_loop();
    while (uv_loop_alive(loop)) {
        uv_run(loop, UV_RUN_ONCE);
        jsc_quickjs_pump_microtasks();
    }
}

extern "C" void jsc_uv_run_loop_forever(void) {
    uv_loop_t* loop = jsc_uv_loop();
    while (uv_loop_alive(loop)) {
        uv_run(loop, UV_RUN_ONCE);
        jsc_quickjs_pump_microtasks();
    }
}

#else

#include "../js_value.h"
#include <cstdio>

extern "C" void jsc_uv_set_timeout_ms(JsValue, JsValue) {
    std::fprintf(
        stderr,
        "blue: setTimeout is unavailable without libuv (install libuv-devel).\n");
}
extern "C" void jsc_uv_set_interval_ms(JsValue, JsValue) {
    std::fprintf(
        stderr,
        "blue: setInterval is unavailable without libuv (install libuv-devel).\n");
}
extern "C" void jsc_uv_clear_timer(JsValue) {}
extern "C" void jsc_uv_run_loop_until_idle(void) {}
extern "C" void jsc_uv_run_loop_forever(void) {}

#endif
