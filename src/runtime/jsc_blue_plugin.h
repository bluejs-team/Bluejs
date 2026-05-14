#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct JSContext;

int jsc_blue_plugin_load(struct JSContext* ctx, const char* path_utf8);
void jsc_blue_plugin_install_js(struct JSContext* ctx, void* blue_object);

#ifdef __cplusplus
}
#endif
