#pragma once

#include "../js_value.h"

#ifdef __cplusplus
extern "C" {
#endif

JsValue jsc_quickjs_eval_from_jsvalue(JsValue source, const char* filename_hint);
JsValue jsc_quickjs_function_construct(int argc, const JsValue* argv);

void jsc_quickjs_pump_microtasks(void);

void jsc_quickjs_ensure_runtime(void);

#ifdef __cplusplus
struct JSContext;
extern "C" JSContext* jsc_quickjs_get_context(void);
#endif

#if defined(JSC_QJS_CONTROL_PLANE) || defined(JSC_HYBRID)
#ifdef __cplusplus
struct JSContext;
extern "C" void jsc_qjs_install_control_plane_shims(struct JSContext* ctx);
#else
void jsc_qjs_install_control_plane_shims(struct JSContext* ctx);
#endif
#endif

typedef JsValue (*jsc_hybrid_aot_dispatch_fn)(const char* function_name,
                                              const char* json_in_utf8);
extern jsc_hybrid_aot_dispatch_fn g_aot_dispatch;
void jsc_hybrid_set_aot_dispatch(jsc_hybrid_aot_dispatch_fn fn);
char* jsc_hybrid_call_aot_cstr(const char* function_name,
                               const char* arg_utf8);

#ifdef JSC_HYBRID
void jsc_hybrid_boot_island(const unsigned char* bundle, size_t len);
JsValue jsc_hybrid_island_call_json(const char* export_name,
                                    const char* json_in_utf8);
#endif

#if defined(JSC_QJS_CONTROL_PLANE) || defined(JSC_HYBRID)
void jsc_qjs_dispatch_http_conn(void* opaque, const char* method,
                                 const char* path, const char* body,
                                 const char* headers_text,
                                 int conn_index);
#endif

#ifdef JSC_QJS_CONTROL_PLANE
void jsc_control_plane_run_embedded(const unsigned char* bundle, size_t len);
int  jsc_http_srv_alloc_slot(void);
void jsc_http_srv_set_qjs_opaque(int id, void* opaque);
void jsc_http_srv_listen_slot(int id, int port);
void jsc_http_conn_res_end_cstr(int conn_index, const char* body_utf8);
void jsc_http_conn_res_send_cstr(int conn_index, int status_code,
                                 const char* headers_text,
                                 const char* body_utf8);
void jsc_http_conn_res_send_binary(int conn_index, int status_code,
                                   const char* headers_text,
                                   const unsigned char* body_data, size_t body_len);
void jsc_http_srv_discard_unstarted(int id);
void jsc_qjs_free_http_opaque(void* opaque);
#endif

void jsc_uv_set_timeout_ms(JsValue callback, JsValue delay_ms);
void jsc_uv_set_interval_ms(JsValue callback, JsValue delay_ms);
void jsc_uv_clear_timer(JsValue timer_id_or_undefined);
void jsc_uv_run_loop_until_idle(void);
void jsc_uv_run_loop_forever(void);

JsValue jsc_io_try_member_call(JsValue recv, JsValue methName, int argc,
                               JsValue* argv, int* handled);

JsValue jsc_node_http_module(void);
JsValue jsc_node_https_module(void);
JsValue jsc_node_net_module(void);
JsValue jsc_node_stream_module(void);
JsValue jsc_node_events_module(void);
JsValue jsc_node_crypto_module(void);
JsValue js_http_createServer(JsValue handler);
JsValue js_crypto_randomBytes(JsValue size, JsValue enc);
JsValue js_crypto_createHash(JsValue algo);
JsValue js_net_createServer(JsValue handler);

#ifdef __cplusplus
}
#endif
