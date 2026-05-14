/*
 * jsc_node_io - Node-shaped I/O: http (libuv), crypto (OpenSSL SHA256 when
 * JSC_HAVE_OPENSSL), events, stream/net stubs. Active only when both
 * JSC_RUNTIME_NODE_IO and JSC_HAVE_UV are defined for the generated binary.
 */
#include "../js_value.h"
#include "jsc_runtime.h"

#include <stdlib.h>
#include <string.h>

extern "C" {

#if !defined(JSC_RUNTIME_NODE_IO) || !defined(JSC_HAVE_UV)

JsValue jsc_io_try_member_call(JsValue recv, JsValue methName, int argc,
                               JsValue* argv, int* handled) {
    (void)recv;
    (void)methName;
    (void)argc;
    (void)argv;
    *handled = 0;
    JsValue u;
    u.tag = JS_UNDEFINED;
    u.as.number = 0;
    return u;
}

JsValue jsc_node_http_module(void) {
    JsValue m = js_obj_new();
    return m;
}
JsValue jsc_node_https_module(void) {
    return jsc_node_http_module();
}
JsValue jsc_node_net_module(void) {
    return js_obj_new();
}
JsValue jsc_node_stream_module(void) {
    return js_obj_new();
}
JsValue jsc_node_events_module(void) {
    return js_obj_new();
}
JsValue jsc_node_crypto_module(void) {
    return js_obj_new();
}
JsValue js_http_createServer(JsValue handler) {
    (void)handler;
    JsValue u;
    u.tag = JS_UNDEFINED;
    u.as.number = 0;
    return u;
}
JsValue js_crypto_randomBytes(JsValue size, JsValue enc) {
    (void)size;
    (void)enc;
    JsValue u;
    u.tag = JS_UNDEFINED;
    u.as.number = 0;
    return u;
}
JsValue js_crypto_createHash(JsValue algo) {
    (void)algo;
    JsValue u;
    u.tag = JS_UNDEFINED;
    u.as.number = 0;
    return u;
}
JsValue js_net_createServer(JsValue handler) {
    (void)handler;
    JsValue u;
    u.tag = JS_UNDEFINED;
    u.as.number = 0;
    return u;
}

#else /* JSC_RUNTIME_NODE_IO && JSC_HAVE_UV */

#include <stdio.h>
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <bcrypt.h>
#  ifndef SSIZE_T_DEFINED
     typedef SSIZE_T ssize_t;
#    define SSIZE_T_DEFINED
#  endif
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <unistd.h>
#endif

#include <uv.h>

#if defined(JSC_HAVE_OPENSSL)
#include <openssl/sha.h>
#endif

JsValue js_crypto_randomBytes(JsValue size, JsValue enc) {
    (void)enc;
    int n = (int)js_to_num(size);
    if (n < 1 || n > 65536)
        return (JsValue){JS_UNDEFINED, {0}};
    unsigned char* p = (unsigned char*)malloc((size_t)n);
    if (!p)
        return (JsValue){JS_UNDEFINED, {0}};
#ifdef _WIN32
    if (BCryptGenRandom(NULL, p, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        free(p);
        return (JsValue){JS_UNDEFINED, {0}};
    }
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        free(p);
        return (JsValue){JS_UNDEFINED, {0}};
    }
    ssize_t r = read(fd, p, (size_t)n);
    close(fd);
    if (r != n) {
        free(p);
        return (JsValue){JS_UNDEFINED, {0}};
    }
#endif
    return js_byte_buffer_own(p, (size_t)n);
}

JsValue js_crypto_createHash(JsValue algo) {
    (void)algo;
    JsValue h = js_obj_new();
    js_obj_set(&h, "__kind", JS_STR("hash"));
    js_obj_set(&h, "__buf", JS_STR(""));
    return h;
}

static JsValue _hash_update_w(int argc, JsValue* argv) {
    if (argc < 2 || argv[0].tag != JS_OBJECT || !argv[0].as.obj)
        return (JsValue){JS_UNDEFINED, {0}};
    char* chunk = js_to_cstr(argv[1]);
    if (!chunk)
        return (JsValue){JS_UNDEFINED, {0}};
    JsValue cur = js_obj_get(argv[0], JS_STR("__buf"));
    char*   old  = js_to_cstr(cur);
    size_t  ol   = old ? strlen(old) : 0;
    size_t  cl   = strlen(chunk);
    char*   cat  = (char*)malloc(ol + cl + 1);
    if (!cat) {
        free(chunk);
        free(old);
        return (JsValue){JS_UNDEFINED, {0}};
    }
    if (ol)
        memcpy(cat, old, ol);
    memcpy(cat + ol, chunk, cl + 1);
    js_obj_set(&argv[0], "__buf", js_str(cat));
    free(cat);
    free(chunk);
    free(old);
    return argv[0];
}

static JsValue _hash_digest_w(int argc, JsValue* argv) {
    if (argc < 1 || argv[0].tag != JS_OBJECT || !argv[0].as.obj)
        return (JsValue){JS_UNDEFINED, {0}};
    JsValue cur = js_obj_get(argv[0], JS_STR("__buf"));
    char*   s    = js_to_cstr(cur);
    if (!s)
        s = js_strdup("");
#if defined(JSC_HAVE_OPENSSL)
    unsigned char md[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char*)s, strlen(s), md);
    static const char* hx = "0123456789abcdef";
    char                hex[SHA256_DIGEST_LENGTH * 2 + 1];
    for (unsigned i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        hex[i * 2]     = hx[md[i] >> 4];
        hex[i * 2 + 1] = hx[md[i] & 15];
    }
    hex[SHA256_DIGEST_LENGTH * 2] = '\0';
    JsValue r = js_str(hex);
    free(s);
    return r;
#else
    unsigned long h = 5381;
    for (const char* p = s; *p; ++p)
        h = ((h << 5) + h) + (unsigned char)*p;
    char hex[40];
    snprintf(hex, sizeof(hex), "%016lx%016lx", h, (unsigned long)strlen(s));
    JsValue r = js_str(hex);
    free(s);
    return r;
#endif
}

static JsValue _crypto_rb_w(int argc, JsValue* argv) {
    return js_crypto_randomBytes(argc > 0 ? argv[0] : (JsValue){JS_UNDEFINED, {0}},
                                 argc > 1 ? argv[1] : (JsValue){JS_UNDEFINED, {0}});
}
static JsValue _crypto_ch_w(int argc, JsValue* argv) {
    return js_crypto_createHash(argc > 0 ? argv[0] : (JsValue){JS_UNDEFINED, {0}});
}

static JsValue _crypto_tseq_w(int argc, JsValue* argv) {
    if (argc < 2)
        return JS_BOOL(0);
    char*                fal = NULL;
    char*                fbl = NULL;
    const unsigned char* pa  = NULL;
    const unsigned char* pb  = NULL;
    size_t               la = 0, lb = 0;
    if (argv[0].tag == JS_BYTES && argv[0].as.bytes) {
        pa = argv[0].as.bytes->data;
        la = argv[0].as.bytes->len;
    } else {
        fal = js_to_cstr(argv[0]);
        pa  = (const unsigned char*)fal;
        la  = fal ? strlen(fal) : 0;
    }
    if (argv[1].tag == JS_BYTES && argv[1].as.bytes) {
        pb = argv[1].as.bytes->data;
        lb = argv[1].as.bytes->len;
    } else {
        fbl = js_to_cstr(argv[1]);
        pb  = (const unsigned char*)fbl;
        lb  = fbl ? strlen(fbl) : 0;
    }
    int ok = 0;
    if (pa && pb && la == lb && la > 0) {
        unsigned char diff = 0;
        for (size_t i = 0; i < la; ++i)
            diff |= (unsigned char)(pa[i] ^ pb[i]);
        ok = (diff == 0);
    }
    free(fal);
    free(fbl);
    return JS_BOOL(ok);
}

JsValue jsc_node_crypto_module(void) {
    JsValue m = js_obj_new();
    js_obj_set_fn(&m, "randomBytes", _crypto_rb_w);
    js_obj_set_fn(&m, "createHash", _crypto_ch_w);
    js_obj_set_fn(&m, "timingSafeEqual", _crypto_tseq_w);
    return m;
}

static JsValue _ee_on_w(int argc, JsValue* argv);
static JsValue _ee_emit_w(int argc, JsValue* argv);

static JsValue _emitter_ctor(int argc, JsValue* argv) {
    (void)argc;
    (void)argv;
    JsValue o = js_obj_new();
    js_obj_set_fn(&o, "on", _ee_on_w);
    js_obj_set_fn(&o, "emit", _ee_emit_w);
    return o;
}

static JsValue _ee_on_w(int argc, JsValue* argv) {
    if (argc < 3)
        return (JsValue){JS_UNDEFINED, {0}};
    char* en = js_to_cstr(argv[1]);
    if (!en)
        return (JsValue){JS_UNDEFINED, {0}};
    char key[96];
    snprintf(key, sizeof(key), "_L:%s", en);
    free(en);
    js_obj_set(&argv[0], key, argv[2]);
    return (JsValue){JS_UNDEFINED, {0}};
}

static JsValue _ee_emit_w(int argc, JsValue* argv) {
    if (argc < 2)
        return (JsValue){JS_UNDEFINED, {0}};
    char* en = js_to_cstr(argv[1]);
    if (!en)
        return (JsValue){JS_UNDEFINED, {0}};
    char key[96];
    snprintf(key, sizeof(key), "_L:%s", en);
    free(en);
    JsValue fn = js_obj_get(argv[0], JS_STR(key));
    if (fn.tag == JS_FUNCTION && fn.as.func) {
        JsValue a[8];
        int     na = argc - 2;
        if (na > 8)
            na = 8;
        for (int i = 0; i < na; ++i)
            a[i] = argv[2 + i];
        return js_call_argv(fn, na, a);
    }
    return (JsValue){JS_UNDEFINED, {0}};
}

JsValue jsc_node_events_module(void) {
    JsValue m = js_obj_new();
    js_obj_set_fn(&m, "EventEmitter", _emitter_ctor);
    return m;
}

static JsValue _noop_ctor(int, JsValue*) {
    return js_obj_new();
}

JsValue jsc_node_stream_module(void) {
    JsValue m = js_obj_new();
    js_obj_set_fn(&m, "Readable", _noop_ctor);
    js_obj_set_fn(&m, "Writable", _noop_ctor);
    js_obj_set_fn(&m, "Duplex", _noop_ctor);
    return m;
}

static JsValue _net_createServer_w(int argc, JsValue* argv);

JsValue jsc_node_net_module(void) {
    JsValue m = js_obj_new();
    js_obj_set_fn(&m, "createServer", _net_createServer_w);
    return m;
}

typedef struct ConnCtx ConnCtx;
struct ConnCtx {
    int      busy;
    uv_tcp_t client;
    char     rdbuf[16384];
    size_t   rdlen;
    JsValue  handler;
    int      handler_set;
#ifdef JSC_QJS_CONTROL_PLANE
    void* qjs_opaque;
#endif
};

typedef struct {
    int       id;
    char      inuse;
    uv_tcp_t  server;
    JsValue   handler;
    int       handler_set;
    uv_loop_t* loop;
#ifdef JSC_QJS_CONTROL_PLANE
    void* qjs_opaque; /* QjsHttpHandler* - QuickJS control plane only */
#endif
} HttpSrv;

static HttpSrv g_http_srv[8];
static ConnCtx g_conn_pool[64];

static int alloc_srv(void) {
    for (int i = 0; i < 8; ++i) {
        if (!g_http_srv[i].inuse) {
            memset(&g_http_srv[i], 0, sizeof(g_http_srv[i]));
            g_http_srv[i].inuse = 1;
            g_http_srv[i].id    = i;
            g_http_srv[i].loop  = uv_default_loop();
#ifdef JSC_QJS_CONTROL_PLANE
            g_http_srv[i].qjs_opaque = nullptr;
#endif
            return i;
        }
    }
    return -1;
}

static void http_srv_close_cb(uv_handle_t* h) {
    HttpSrv* hs = (HttpSrv*)h->data;
    if (hs) {
#ifdef JSC_QJS_CONTROL_PLANE
        if (hs->qjs_opaque) {
            jsc_qjs_free_http_opaque(hs->qjs_opaque);
            hs->qjs_opaque = nullptr;
        }
#endif
        memset(hs, 0, sizeof(HttpSrv));
    }
}

static void free_srv_slot(int i) {
    if (i < 0 || i >= 8)
        return;
    HttpSrv* hs = &g_http_srv[i];
    if (!hs->inuse)
        return;
    hs->server.data = hs;
    uv_close((uv_handle_t*)&hs->server, http_srv_close_cb);
}

static ConnCtx* alloc_conn(void) {
    for (int i = 0; i < 64; ++i) {
        if (!g_conn_pool[i].busy) {
            memset(&g_conn_pool[i], 0, sizeof(g_conn_pool[i]));
            g_conn_pool[i].busy = 1;
            return &g_conn_pool[i];
        }
    }
    return nullptr;
}

static void free_conn(ConnCtx* c) {
    if (!c)
        return;
    c->busy        = 0;
    c->handler_set = 0;
    c->handler     = (JsValue){JS_UNDEFINED, {0}};
#ifdef JSC_QJS_CONTROL_PLANE
    c->qjs_opaque = nullptr;
#endif
    c->rdlen       = 0;
    memset(c->rdbuf, 0, sizeof(c->rdbuf));
    memset(&c->client, 0, sizeof(c->client));
}

static void conn_close_cb(uv_handle_t* h) {
    ConnCtx* c = (ConnCtx*)h->data;
    if (c)
        free_conn(c);
}

typedef struct {
    uv_write_t w;
    char*      heap;
    uv_stream_t* close_after; /* if set, uv_close this stream after write completes */
} JscWriteCtx;

static void jsc_after_write_cb(uv_write_t* req, int status) {
    (void)status;
    JscWriteCtx* wc = reinterpret_cast<JscWriteCtx*>(req);
    if (wc) {
        uv_stream_t* to_close = wc->close_after;
        free(wc->heap);
        free(wc);
        if (to_close)
            uv_close((uv_handle_t*)to_close, conn_close_cb);
    }
}

static int jsc_uv_write_copy(uv_stream_t* stream, const void* data, size_t len) {
    if (!stream || !data || len == 0)
        return -1;
    char* heap = (char*)malloc(len);
    if (!heap)
        return -1;
    memcpy(heap, data, len);
    auto* wc = (JscWriteCtx*)malloc(sizeof(JscWriteCtx));
    if (!wc) {
        free(heap);
        return -1;
    }
    memset(wc, 0, sizeof(*wc));
    wc->heap        = heap;
    uv_buf_t buf    = uv_buf_init(heap, (unsigned int)len);
    int      st     = uv_write(&wc->w, stream, &buf, 1, jsc_after_write_cb);
    if (st != 0) {
        free(heap);
        free(wc);
        return st;
    }
    return 0;
}

static JsValue make_req(const char* method, const char* url) {
    JsValue req = js_obj_new();
    js_obj_set(&req, "method", js_str(method));
    js_obj_set(&req, "url", js_str(url));
    js_obj_set(&req, "headers", js_obj_new());
    return req;
}

static JsValue _res_end_w(int argc, JsValue* argv);
static JsValue _res_write_w(int argc, JsValue* argv);
static JsValue _res_writeHead_w(int argc, JsValue* argv);

static JsValue make_res(ConnCtx* conn) {
    JsValue res = js_obj_new();
    js_obj_set(&res, "__kind", JS_STR("httpRes"));
    js_obj_set(&res, "__conn", JS_NUM((double)(conn - g_conn_pool)));
    js_obj_set_fn(&res, "writeHead", _res_writeHead_w);
    js_obj_set_fn(&res, "write", _res_write_w);
    js_obj_set_fn(&res, "end", _res_end_w);
    return res;
}

static ConnCtx* res_conn(JsValue res) {
    JsValue idx = js_obj_get(res, JS_STR("__conn"));
    int     i   = (int)js_to_num(idx);
    if (i < 0 || i >= 64)
        return nullptr;
    return &g_conn_pool[i];
}

static JsValue _res_writeHead_w(int argc, JsValue* argv) {
    (void)argc;
    (void)argv;
    return (JsValue){JS_UNDEFINED, {0}};
}

static JsValue _res_write_w(int argc, JsValue* argv) {
    if (argc < 2)
        return (JsValue){JS_UNDEFINED, {0}};
    ConnCtx* c = res_conn(argv[0]);
    if (!c)
        return (JsValue){JS_UNDEFINED, {0}};
    if (argv[1].tag == JS_BYTES && argv[1].as.bytes && argv[1].as.bytes->data) {
        JsBytes* b = argv[1].as.bytes;
        (void)jsc_uv_write_copy((uv_stream_t*)&c->client, b->data, b->len);
    } else {
        char* s = js_to_cstr(argv[1]);
        if (s) {
            (void)jsc_uv_write_copy((uv_stream_t*)&c->client, s, strlen(s));
            free(s);
        }
    }
    return (JsValue){JS_UNDEFINED, {0}};
}

static JsValue _res_end_w(int argc, JsValue* argv) {
    ConnCtx* c = res_conn(argv[0]);
    if (!c)
        return (JsValue){JS_UNDEFINED, {0}};
    const unsigned char* body    = NULL;
    size_t               bodylen = 0;
    char*                owned   = NULL;
    if (argc >= 2) {
        if (argv[1].tag == JS_BYTES && argv[1].as.bytes && argv[1].as.bytes->data) {
            JsBytes* b = argv[1].as.bytes;
            body    = b->data;
            bodylen = b->len;
        } else {
            owned = js_to_cstr(argv[1]);
            if (owned) {
                body    = (const unsigned char*)owned;
                bodylen = strlen(owned);
            }
        }
    }
    char hdr[192];
    int  hl = snprintf(hdr, sizeof(hdr),
                       "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: "
                       "close\r\n\r\n",
                       bodylen);
    if (hl < 0)
        hl = 0;
    size_t           total = (size_t)hl + bodylen;
    unsigned char* pkt = (unsigned char*)malloc(total ? total : 1);
    if (!pkt) {
        free(owned);
        return (JsValue){JS_UNDEFINED, {0}};
    }
    if ((size_t)hl)
        memcpy(pkt, hdr, (size_t)hl);
    if (bodylen && body)
        memcpy(pkt + (size_t)hl, body, bodylen);
    free(owned);
    (void)jsc_uv_write_copy((uv_stream_t*)&c->client, pkt, total);
    free(pkt);
    uv_close((uv_handle_t*)&c->client, conn_close_cb);
    return (JsValue){JS_UNDEFINED, {0}};
}

static void handle_http(ConnCtx* conn, const char* data, size_t len) {
#ifdef JSC_QJS_CONTROL_PLANE
    if (conn->qjs_opaque) {
        char method[16], path[1024];
        char tmp[16384];
        size_t copy = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
        memcpy(tmp, data, copy);
        tmp[copy] = '\0';
        char* le = strstr(tmp, "\r\n");
        if (!le)
            return;
        *le = '\0';
        if (sscanf(tmp, "%15s %1023s", method, path) < 2)
            return;
        if (strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0) {
            const char* scheme = strstr(path, "://");
            const char* slash = scheme ? strchr(scheme + 3, '/') : nullptr;
            if (slash) {
                size_t n = strlen(slash);
                if (n >= sizeof(path))
                    n = sizeof(path) - 1;
                memmove(path, slash, n);
                path[n] = '\0';
            } else {
                strcpy(path, "/");
            }
        }
        char headers[4096];
        headers[0] = '\0';
        const char* hs = le + 2;
        const char* end = strstr(hs, "\r\n\r\n");
        if (end && end > hs) {
            size_t hlen = (size_t)(end - hs);
            if (hlen >= sizeof(headers))
                hlen = sizeof(headers) - 1;
            memcpy(headers, hs, hlen);
            headers[hlen] = '\0';
        }
        char bodyBuf[8192];
        bodyBuf[0] = '\0';
        if (end) {
            const char* bodyStart = end + 4;
            size_t avail = 0;
            if (bodyStart >= tmp && bodyStart <= (tmp + copy))
                avail = copy - (size_t)(bodyStart - tmp);
            size_t want = avail;
            const char* clh = strstr(headers, "content-length:");
            if (!clh)
                clh = strstr(headers, "Content-Length:");
            if (clh) {
                const char* p = strchr(clh, ':');
                if (p)
                    want = (size_t)strtoul(p + 1, NULL, 10);
            }
            if (clh && avail < want)
                return; /* wait for full body before dispatch */
            if (want > avail)
                want = avail;
            if (want >= sizeof(bodyBuf))
                want = sizeof(bodyBuf) - 1;
            if (want > 0)
                memcpy(bodyBuf, bodyStart, want);
            bodyBuf[want] = '\0';
        }
        jsc_qjs_dispatch_http_conn(conn->qjs_opaque, method, path, bodyBuf, headers,
                                   (int)(conn - g_conn_pool));
        return;
    }
#endif
    if (!conn->handler_set)
        return;
    char method[16], path[1024];
    char tmp[16384];
    size_t copy = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
    memcpy(tmp, data, copy);
    tmp[copy] = '\0';
    char* le = strstr(tmp, "\r\n");
    if (!le)
        return;
    *le = '\0';
    if (sscanf(tmp, "%15s %1023s", method, path) < 2)
        return;
    JsValue req = make_req(method, path);
    JsValue res = make_res(conn);
    JsValue a[2] = {req, res};
    (void)js_call_argv(conn->handler, 2, a);
}

static void read_cb(uv_stream_t* s, ssize_t nread, const uv_buf_t* buf) {
    ConnCtx* c = (ConnCtx*)s->data;
    if (!c) {
        if (nread > 0 && buf->base)
            free(buf->base);
        return;
    }
    if (nread < 0) {
        if (buf->base)
            free(buf->base);
        uv_close((uv_handle_t*)s, conn_close_cb);
        return;
    }
    if (nread == 0) {
        if (buf->base)
            free(buf->base);
        return;
    }
    if (c->rdlen + (size_t)nread < sizeof(c->rdbuf)) {
        memcpy(c->rdbuf + c->rdlen, buf->base, (size_t)nread);
        c->rdlen += (size_t)nread;
    }
    if (buf->base)
        free(buf->base);
    if (strstr(c->rdbuf, "\r\n\r\n"))
        handle_http(c, c->rdbuf, c->rdlen);
}

static void alloc_cb(uv_handle_t*, size_t suggested, uv_buf_t* buf) {
    buf->base = (char*)malloc(suggested ? suggested : 4096);
    buf->len  = buf->base ? (suggested ? suggested : 4096) : 0;
}

static void on_connection(uv_stream_t* srv, int status) {
    if (status < 0)
        return;
    HttpSrv* hs = (HttpSrv*)srv->data;
    ConnCtx* c = alloc_conn();
    if (!c || !hs)
        return;
    uv_tcp_init(hs->loop, &c->client);
    c->client.data = c;
    if (uv_accept(srv, (uv_stream_t*)&c->client) != 0) {
        free_conn(c);
        return;
    }
    c->handler       = hs->handler;
    c->handler_set   = hs->handler_set;
#ifdef JSC_QJS_CONTROL_PLANE
    c->qjs_opaque = hs->qjs_opaque;
#endif
    uv_read_start((uv_stream_t*)&c->client, alloc_cb, read_cb);
}

JsValue js_http_createServer(JsValue handler) {
    int id = alloc_srv();
    if (id < 0)
        return (JsValue){JS_UNDEFINED, {0}};
    g_http_srv[id].handler = js_clone_value(handler);
    g_http_srv[id].handler_set =
        (handler.tag == JS_FUNCTION && handler.as.func) ? 1 : 0;
#ifdef JSC_QJS_CONTROL_PLANE
    g_http_srv[id].qjs_opaque = nullptr;
#endif
    JsValue o = js_obj_new();
    js_obj_set(&o, "__kind", JS_STR("httpServer"));
    js_obj_set(&o, "__sid", JS_NUM((double)id));
    return o;
}

#ifdef JSC_QJS_CONTROL_PLANE
static JsValue http_listen_impl(JsValue server, int argc, JsValue* argv);

int jsc_http_srv_alloc_slot(void) {
    return alloc_srv();
}

void jsc_http_srv_set_qjs_opaque(int id, void* opaque) {
    if (id < 0 || id >= 8 || !g_http_srv[id].inuse)
        return;
    g_http_srv[id].qjs_opaque   = opaque;
    g_http_srv[id].handler_set = 0;
    g_http_srv[id].handler     = (JsValue){JS_UNDEFINED, {0}};
}

void jsc_http_srv_listen_slot(int id, int port) {
    JsValue fake = js_obj_new();
    js_obj_set(&fake, "__kind", JS_STR("httpServer"));
    js_obj_set(&fake, "__sid", JS_NUM((double)id));
    JsValue a[1] = {JS_NUM((double)port)};
    (void)http_listen_impl(fake, 1, a);
}

static const char* jsc_http_reason_phrase(int status_code) {
    switch (status_code) {
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    default:  return "OK";
    }
}

void jsc_http_conn_res_send_cstr(int conn_index, int status_code,
                                 const char* headers_text,
                                 const char* body_utf8) {
    if (conn_index < 0 || conn_index >= 64 || !g_conn_pool[conn_index].busy)
        return;
    ConnCtx* c = &g_conn_pool[conn_index];
    const char* body = body_utf8 ? body_utf8 : "";
    size_t bodylen = strlen(body);
    if (status_code < 100 || status_code > 999)
        status_code = 200;

    char headerBuf[8192];
    size_t off = 0;
    int n = snprintf(headerBuf + off, sizeof(headerBuf) - off,
                     "HTTP/1.1 %d %s\r\n", status_code,
                     jsc_http_reason_phrase(status_code));
    if (n < 0)
        return;
    off += (size_t)n;

    int hasContentLength = 0;
    if (headers_text && *headers_text) {
        const char* p = headers_text;
        while (*p && off + 4 < sizeof(headerBuf)) {
            const char* lineEnd = strstr(p, "\r\n");
            size_t lineLen = lineEnd ? (size_t)(lineEnd - p) : strlen(p);
            if (lineLen > 0) {
                if (lineLen >= 15) {
                    char keyLower[32];
                    size_t kl = lineLen < sizeof(keyLower) - 1 ? lineLen : sizeof(keyLower) - 1;
                    memcpy(keyLower, p, kl);
                    keyLower[kl] = '\0';
                    for (size_t i = 0; i < kl; ++i) {
                        if (keyLower[i] >= 'A' && keyLower[i] <= 'Z')
                            keyLower[i] = (char)(keyLower[i] - 'A' + 'a');
                    }
                    if (strncmp(keyLower, "content-length:", 15) == 0)
                        hasContentLength = 1;
                }
                if (off + lineLen + 2 >= sizeof(headerBuf))
                    break;
                memcpy(headerBuf + off, p, lineLen);
                off += lineLen;
                headerBuf[off++] = '\r';
                headerBuf[off++] = '\n';
            }
            if (!lineEnd)
                break;
            p = lineEnd + 2;
        }
    }
    if (!hasContentLength) {
        n = snprintf(headerBuf + off, sizeof(headerBuf) - off,
                     "Content-Length: %zu\r\n", bodylen);
        if (n < 0)
            return;
        off += (size_t)n;
    }
    n = snprintf(headerBuf + off, sizeof(headerBuf) - off,
                 "Connection: close\r\n\r\n");
    if (n < 0)
        return;
    off += (size_t)n;

    size_t total = off + bodylen;
    unsigned char* pkt = (unsigned char*)malloc(total ? total : 1);
    if (!pkt)
        return;
    memcpy(pkt, headerBuf, off);
    if (bodylen)
        memcpy(pkt + off, body, bodylen);
    (void)jsc_uv_write_copy((uv_stream_t*)&c->client, pkt, total);
    free(pkt);
    uv_close((uv_handle_t*)&c->client, conn_close_cb);
}

void jsc_http_conn_res_send_binary(int conn_index, int status_code,
                                   const char* headers_text,
                                   const uint8_t* body_data, size_t body_len) {
    if (conn_index < 0 || conn_index >= 64 || !g_conn_pool[conn_index].busy)
        return;
    ConnCtx* c = &g_conn_pool[conn_index];
    if (status_code < 100 || status_code > 999)
        status_code = 200;

    char headerBuf[8192];
    size_t off = 0;
    int n = snprintf(headerBuf + off, sizeof(headerBuf) - off,
                     "HTTP/1.1 %d %s\r\n", status_code,
                     jsc_http_reason_phrase(status_code));
    if (n < 0)
        return;
    off += (size_t)n;

    int hasContentLength = 0;
    if (headers_text && *headers_text) {
        const char* p = headers_text;
        while (*p && off + 4 < sizeof(headerBuf)) {
            const char* lineEnd = strstr(p, "\r\n");
            size_t lineLen = lineEnd ? (size_t)(lineEnd - p) : strlen(p);
            if (lineLen > 0) {
                if (lineLen >= 15) {
                    char keyLower[32];
                    size_t kl = lineLen < sizeof(keyLower) - 1 ? lineLen : sizeof(keyLower) - 1;
                    memcpy(keyLower, p, kl);
                    keyLower[kl] = '\0';
                    for (size_t i = 0; i < kl; ++i) {
                        if (keyLower[i] >= 'A' && keyLower[i] <= 'Z')
                            keyLower[i] = (char)(keyLower[i] - 'A' + 'a');
                    }
                    if (strncmp(keyLower, "content-length:", 15) == 0)
                        hasContentLength = 1;
                }
                if (off + lineLen + 2 >= sizeof(headerBuf))
                    break;
                memcpy(headerBuf + off, p, lineLen);
                off += lineLen;
                headerBuf[off++] = '\r';
                headerBuf[off++] = '\n';
            }
            if (!lineEnd)
                break;
            p = lineEnd + 2;
        }
    }
    if (!hasContentLength) {
        n = snprintf(headerBuf + off, sizeof(headerBuf) - off,
                     "Content-Length: %zu\r\n", body_len);
        if (n < 0)
            return;
        off += (size_t)n;
    }
    n = snprintf(headerBuf + off, sizeof(headerBuf) - off,
                 "Connection: close\r\n\r\n");
    if (n < 0)
        return;
    off += (size_t)n;

    size_t total = off + body_len;
    unsigned char* pkt = (unsigned char*)malloc(total ? total : 1);
    if (!pkt)
        return;
    memcpy(pkt, headerBuf, off);
    if (body_len && body_data)
        memcpy(pkt + off, body_data, body_len);
    /* Queue write and close the connection only after the write completes,
     * so that large binary payloads are fully flushed before the TCP FIN. */
    {
        char* heap = (char*)malloc(total);
        if (!heap) { free(pkt); return; }
        memcpy(heap, pkt, total);
        free(pkt);
        auto* wc = (JscWriteCtx*)malloc(sizeof(JscWriteCtx));
        if (!wc) { free(heap); return; }
        memset(wc, 0, sizeof(*wc));
        wc->heap = heap;
        wc->close_after = (uv_stream_t*)&c->client;
        uv_buf_t buf = uv_buf_init(heap, (unsigned int)total);
        int st = uv_write(&wc->w, (uv_stream_t*)&c->client, &buf, 1, jsc_after_write_cb);
        if (st != 0) {
            free(heap);
            free(wc);
            uv_close((uv_handle_t*)&c->client, conn_close_cb);
        }
    }
}

void jsc_http_conn_res_end_cstr(int conn_index, const char* body_utf8) {
    jsc_http_conn_res_send_cstr(conn_index, 200, nullptr, body_utf8);
}

void jsc_http_srv_discard_unstarted(int id) {
    if (id < 0 || id >= 8)
        return;
    HttpSrv* hs = &g_http_srv[id];
    if (!hs->inuse)
        return;
#ifdef JSC_QJS_CONTROL_PLANE
    hs->qjs_opaque = nullptr;
#endif
    memset(hs, 0, sizeof(*hs));
}
#endif /* JSC_QJS_CONTROL_PLANE */

static JsValue http_listen_impl(JsValue server, int argc, JsValue* argv) {
    JsValue skey = JS_STR("__sid");
    JsValue sid = js_obj_get(server, skey);
    js_dispose_value(&skey);
    int     id  = (int)js_to_num(sid);
    js_dispose_value(&sid);
    if (id < 0 || id >= 8)
        return (JsValue){JS_UNDEFINED, {0}};
    HttpSrv* hs = &g_http_srv[id];
    int      port = 8080;
    if (argc >= 1)
        port = (int)js_to_num(argv[0]);
    if (port <= 0 || port > 65535)
        port = 8080;
    uv_tcp_init(hs->loop, &hs->server);
    hs->server.data = hs;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (uv_tcp_bind(&hs->server, (const struct sockaddr*)&addr, 0) != 0) {
        free_srv_slot(id);
        return (JsValue){JS_UNDEFINED, {0}};
    }
    uv_listen((uv_stream_t*)&hs->server, 128, on_connection);
    return js_clone_value(argv[0]);
}

static JsValue _http_createServer_wrap(int argc, JsValue* argv) {
    return js_http_createServer(argc > 0 ? argv[0] : (JsValue){JS_UNDEFINED, {0}});
}

JsValue jsc_node_http_module(void) {
    JsValue m = js_obj_new();
    js_obj_set_fn(&m, "createServer", _http_createServer_wrap);
    return m;
}

JsValue jsc_node_https_module(void) {
    return jsc_node_http_module();
}

typedef struct {
    int        active;
    int        id;
    char       inuse;
    uv_tcp_t   server;
    JsValue    conn_cb;
    int        conn_cb_set;
    uv_loop_t* loop;
} NetSrv;

typedef struct {
    int       active;
    int       pool_idx;
    uv_tcp_t  client;
    JsValue   on_data;
    int       on_data_set;
} NetConn;

static NetSrv  g_net_srv[4];
static NetConn g_net_conn[64];

static void net_conn_close_cb(uv_handle_t* h) {
    NetConn* nc = (NetConn*)h->data;
    if (!nc)
        return;
    nc->active       = 0;
    nc->on_data_set = 0;
    nc->on_data     = (JsValue){JS_UNDEFINED, {0}};
    memset(&nc->client, 0, sizeof(nc->client));
}

static int alloc_net_conn(void) {
    for (int i = 0; i < 64; ++i) {
        if (!g_net_conn[i].active) {
            memset(&g_net_conn[i], 0, sizeof(NetConn));
            g_net_conn[i].active   = 1;
            g_net_conn[i].pool_idx = i;
            return i;
        }
    }
    return -1;
}

static void net_read_cb(uv_stream_t* s, ssize_t nread, const uv_buf_t* buf) {
    NetConn* nc = (NetConn*)s->data;
    if (!nc) {
        if (nread > 0 && buf->base)
            free(buf->base);
        return;
    }
    if (nread < 0) {
        if (buf->base)
            free(buf->base);
        uv_close((uv_handle_t*)s, net_conn_close_cb);
        return;
    }
    if (nread == 0) {
        if (buf->base)
            free(buf->base);
        return;
    }
    if (nc->on_data_set && nc->on_data.tag == JS_FUNCTION && nc->on_data.as.func) {
        unsigned char* cp = (unsigned char*)malloc((size_t)nread);
        if (cp) {
            memcpy(cp, buf->base, (size_t)nread);
            JsValue chunk = js_byte_buffer_own(cp, (size_t)nread);
            JsValue a[1]  = {chunk};
            (void)js_call_argv(nc->on_data, 1, a);
        }
    }
    if (buf->base)
        free(buf->base);
}

static void net_on_connection(uv_stream_t* srv, int status) {
    if (status < 0)
        return;
    NetSrv* ns = (NetSrv*)srv->data;
    int     ci = alloc_net_conn();
    if (ci < 0 || !ns)
        return;
    NetConn* nc = &g_net_conn[ci];
    uv_tcp_init(ns->loop, &nc->client);
    nc->client.data = nc;
    if (uv_accept(srv, (uv_stream_t*)&nc->client) != 0) {
        nc->active = 0;
        memset(nc, 0, sizeof(*nc));
        return;
    }
    if (ns->conn_cb_set && ns->conn_cb.tag == JS_FUNCTION && ns->conn_cb.as.func) {
        JsValue sock = js_obj_new();
        js_obj_set(&sock, "__kind", JS_STR("netSocket"));
        js_obj_set(&sock, "__ncid", JS_NUM((double)ci));
        JsValue a[1] = {sock};
        (void)js_call_argv(ns->conn_cb, 1, a);
    }
    uv_read_start((uv_stream_t*)&nc->client, alloc_cb, net_read_cb);
}

static void net_srv_close_cb(uv_handle_t* h) {
    NetSrv* ns = (NetSrv*)h->data;
    if (ns)
        memset(ns, 0, sizeof(NetSrv));
}

static int alloc_net_srv(void) {
    for (int i = 0; i < 4; ++i) {
        if (!g_net_srv[i].inuse) {
            memset(&g_net_srv[i], 0, sizeof(NetSrv));
            g_net_srv[i].inuse = 1;
            g_net_srv[i].id    = i;
            g_net_srv[i].loop  = uv_default_loop();
            return i;
        }
    }
    return -1;
}

static void free_net_srv_slot(int i) {
    if (i < 0 || i >= 4)
        return;
    NetSrv* ns = &g_net_srv[i];
    if (!ns->inuse)
        return;
    ns->server.data = ns;
    uv_close((uv_handle_t*)&ns->server, net_srv_close_cb);
}

static JsValue net_listen_impl(JsValue server, int argc, JsValue* argv) {
    JsValue nkey = JS_STR("__nid");
    JsValue sid = js_obj_get(server, nkey);
    js_dispose_value(&nkey);
    int     id  = (int)js_to_num(sid);
    js_dispose_value(&sid);
    if (id < 0 || id >= 4)
        return (JsValue){JS_UNDEFINED, {0}};
    NetSrv* ns = &g_net_srv[id];
    int     port = 9000;
    if (argc >= 1)
        port = (int)js_to_num(argv[0]);
    if (port <= 0 || port > 65535)
        port = 9000;
    uv_tcp_init(ns->loop, &ns->server);
    ns->server.data = ns;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (uv_tcp_bind(&ns->server, (const struct sockaddr*)&addr, 0) != 0) {
        free_net_srv_slot(id);
        return (JsValue){JS_UNDEFINED, {0}};
    }
    uv_listen((uv_stream_t*)&ns->server, 128, net_on_connection);
    return (JsValue){JS_UNDEFINED, {0}};
}

JsValue js_net_createServer(JsValue handler) {
    int id = alloc_net_srv();
    if (id < 0)
        return (JsValue){JS_UNDEFINED, {0}};
    g_net_srv[id].conn_cb = handler;
    g_net_srv[id].conn_cb_set =
        (handler.tag == JS_FUNCTION && handler.as.func) ? 1 : 0;
    JsValue o = js_obj_new();
    js_obj_set(&o, "__kind", JS_STR("netServer"));
    js_obj_set(&o, "__nid", JS_NUM((double)id));
    return o;
}

static JsValue _net_createServer_w(int argc, JsValue* argv) {
    return js_net_createServer(argc > 0 ? argv[0] : (JsValue){JS_UNDEFINED, {0}});
}

JsValue jsc_io_try_member_call(JsValue recv, JsValue methName, int argc,
                               JsValue* argv, int* handled) {
    *handled = 0;
    char* mn = js_to_cstr(methName);
    if (!mn) {
        JsValue u;
        u.tag = JS_UNDEFINED;
        u.as.number = 0;
        return u;
    }
    if (recv.tag == JS_OBJECT && recv.as.obj) {
        JsValue kind = js_obj_get(recv, JS_STR("__kind"));
        if (kind.tag == JS_STRING && kind.as.string &&
            strcmp(kind.as.string, "httpServer") == 0 &&
            strcmp(mn, "listen") == 0) {
            *handled = 1;
            free(mn);
            return http_listen_impl(recv, argc, argv);
        }
        if (kind.tag == JS_STRING && kind.as.string &&
            strcmp(kind.as.string, "netServer") == 0 &&
            strcmp(mn, "listen") == 0) {
            *handled = 1;
            free(mn);
            return net_listen_impl(recv, argc, argv);
        }
        if (kind.tag == JS_STRING && kind.as.string &&
            strcmp(kind.as.string, "netSocket") == 0) {
            if (strcmp(mn, "on") == 0 && argc >= 2) {
                JsValue ixs = js_obj_get(recv, JS_STR("__ncid"));
                int     idx = (int)js_to_num(ixs);
                if (idx >= 0 && idx < 64 && g_net_conn[idx].active) {
                    char* ev = js_to_cstr(argv[0]);
                    if (ev && strcmp(ev, "data") == 0) {
                        g_net_conn[idx].on_data     = argv[1];
                        g_net_conn[idx].on_data_set = 1;
                    }
                    free(ev);
                }
                *handled = 1;
                free(mn);
                return (JsValue){JS_UNDEFINED, {0}};
            }
            if (strcmp(mn, "write") == 0 && argc >= 1) {
                JsValue ixs = js_obj_get(recv, JS_STR("__ncid"));
                int     idx = (int)js_to_num(ixs);
                if (idx >= 0 && idx < 64 && g_net_conn[idx].active) {
                    NetConn* nc = &g_net_conn[idx];
                    if (argv[0].tag == JS_BYTES && argv[0].as.bytes &&
                        argv[0].as.bytes->data) {
                        JsBytes* b = argv[0].as.bytes;
                        (void)jsc_uv_write_copy((uv_stream_t*)&nc->client, b->data,
                                                b->len);
                    } else {
                        char* s = js_to_cstr(argv[0]);
                        if (s) {
                            (void)jsc_uv_write_copy((uv_stream_t*)&nc->client, s,
                                                    strlen(s));
                            free(s);
                        }
                    }
                }
                *handled = 1;
                free(mn);
                return (JsValue){JS_UNDEFINED, {0}};
            }
        }
        if (kind.tag == JS_STRING && kind.as.string &&
            strcmp(kind.as.string, "hash") == 0) {
            if (strcmp(mn, "update") == 0) {
                JsValue a[2] = {recv, argc > 0 ? argv[0] : (JsValue){JS_UNDEFINED, {0}}};
                *handled = 1;
                free(mn);
                return _hash_update_w(2, a);
            }
            if (strcmp(mn, "digest") == 0) {
                *handled = 1;
                free(mn);
                return _hash_digest_w(1, &recv);
            }
        }
    }
    free(mn);
    JsValue u;
    u.tag = JS_UNDEFINED;
    u.as.number = 0;
    return u;
}

#endif /* JSC_RUNTIME_NODE_IO && JSC_HAVE_UV */

} /* extern "C" */

/* Single copy for js_node.h `extern jsc_argc` / `jsc_argv` (blue link unit). */
extern "C" {
int    jsc_argc  = 0;
char** jsc_argv = nullptr;
}
