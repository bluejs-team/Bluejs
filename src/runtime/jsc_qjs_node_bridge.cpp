/*
 * Control plane: QuickJS runs bundled npm/app JS; data plane (fs/http TCP)
 * stays in C++ (jsc_node_io, js_node.h) and is reached only via FFI here.
 *
 * Built only with -DJSC_QJS_CONTROL_PLANE (see main.cpp -build).
 */

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <quickjs.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#undef JS_UNDEFINED
#undef JS_NULL
#undef JS_FALSE
#undef JS_TRUE
#undef JS_BOOL

#include "../js_value.h"
#include "../js_node.h"
#include "jsc_blue_qjs.h"
#include "jsc_runtime.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if (defined(JSC_QJS_CONTROL_PLANE) || defined(JSC_HYBRID)) && \
    defined(JSC_RUNTIME_NODE_IO) && defined(JSC_HAVE_UV)

extern "C" JSValue jsc_quickjs_jsvalue_to_qjs(JsValue jv);

static void blue_write_boot_metric_ms(long long ms) {
    const char* p = std::getenv("BLUE_BOOT_METRICS_FILE");
    if (!p || !*p)
        return;
    std::fflush(stdout);
    std::fflush(stderr);
    if (FILE* f = std::fopen(p, "w")) {
        std::fprintf(f, "%lld\n", ms);
        std::fclose(f);
    }
}

static inline JSValue qjs_undef(void) {
    return JS_MKVAL(JS_TAG_UNDEFINED, 0);
}

static JSValue qjs_from_jsnode_builtin(JSContext* ctx, const char* spec) {
    (void)ctx;
    JsValue spec_v = js_str(spec ? spec : "");
    JsValue mod = js_node_require(spec_v);
    js_dispose_value(&spec_v);
    if (mod.tag == JS_UNDEFINED)
        return qjs_undef();
    JSValue qmod = jsc_quickjs_jsvalue_to_qjs(mod);
    js_dispose_value(&mod);
    return qmod;
}

/** QuickJS often has no `console`; island code uses this for real stderr output. */
static JSValue qjs_console_print_line(JSContext* ctx, JSValueConst this_val,
                                      int argc, JSValueConst* argv) {
    (void)this_val;
    const char* s = nullptr;
    if (argc >= 1)
        s = JS_ToCString(ctx, argv[0]);
    std::fprintf(stderr, "%s\n", s && s[0] ? s : "");
    std::fflush(stderr);
    if (s)
        JS_FreeCString(ctx, s);
    return qjs_undef();
}

struct QjsHttpHandler {
    JSContext* ctx;
    JSValue    fn;
};

static void free_qjs_http_handler(QjsHttpHandler* h) {
    if (!h)
        return;
    if (h->ctx && !JS_IsUndefined(h->fn) && !JS_IsNull(h->fn))
        JS_FreeValue(h->ctx, h->fn);
    std::free(h);
}

extern "C" void jsc_qjs_free_http_opaque(void* opaque) {
    free_qjs_http_handler((QjsHttpHandler*)opaque);
}

static JSValue __attribute__((unused))
qjs_fs_readFileSync(JSContext* ctx, JSValueConst this_val, int argc,
                    JSValueConst* argv) {
    (void)this_val;
    const char* path = nullptr;
    if (argc >= 1) {
        path = JS_ToCString(ctx, argv[0]);
        if (!path)
            return qjs_undef();
    }
    JsValue jpath = js_str(path ? path : "");
    if (path)
        JS_FreeCString(ctx, path);
    JsValue out = js_fs_readFileSync(jpath, JS_UNDEFINED());
    js_dispose_value(&jpath);
    JSValue qout = jsc_quickjs_jsvalue_to_qjs(out);
    js_dispose_value(&out);
    return qout;
}

static JSValue qjs_http_listen(JSContext* ctx, JSValueConst this_val, int argc,
                               JSValueConst* argv) {
    int sid = -1;
    {
        JSValue v = JS_GetPropertyStr(ctx, this_val, "__sid");
        if (!JS_IsException(v)) {
            int32_t x = 0;
            if (JS_ToInt32(ctx, &x, v) == 0)
                sid = (int)x;
            JS_FreeValue(ctx, v);
        }
    }
    if (sid < 0)
        return qjs_undef();
    int port = 8080;
    if (argc >= 1) {
        int32_t p = 0;
        if (JS_ToInt32(ctx, &p, argv[0]) == 0 && p > 0 && p <= 65535)
            port = (int)p;
    }
    /* Node: listen(port[, host][, backlog][, cb]); demos use listen(port, host, cb). */
    int cb_index = -1;
    if (argc >= 3 && JS_IsFunction(ctx, argv[2]))
        cb_index = 2;
    else if (argc >= 2 && JS_IsFunction(ctx, argv[1]))
        cb_index = 1;

    jsc_http_srv_listen_slot(sid, port);

    if (cb_index >= 0) {
        JSValue ret = JS_Call(ctx, argv[cb_index], this_val, 0, nullptr);
        if (JS_IsException(ret)) {
            JS_FreeValue(ctx, ret);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, ret);
    }
    return qjs_undef();
}

static JSValue qjs_http_createServer(JSContext* ctx, JSValueConst this_val,
                                     int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
        return qjs_undef();

    int sid = jsc_http_srv_alloc_slot();
    if (sid < 0)
        return qjs_undef();

    auto* qh = (QjsHttpHandler*)std::malloc(sizeof(QjsHttpHandler));
    if (!qh) {
        jsc_http_srv_discard_unstarted(sid);
        return qjs_undef();
    }
    qh->ctx = ctx;
    qh->fn  = JS_DupValue(ctx, argv[0]);
    jsc_http_srv_set_qjs_opaque(sid, qh);

    JSValue srv = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, srv, "__sid", JS_NewInt32(ctx, sid));
    JS_SetPropertyStr(
        ctx, srv, "listen",
        JS_NewCFunction(ctx, qjs_http_listen, "listen", 4));
    return srv;
}

static JSValue qjs_express_module(JSContext* ctx) {
    static JSContext* s_ctx = nullptr;
    static JSValue s_mod = {};
    static bool s_has = false;
    if (s_ctx == ctx && s_has && !JS_IsUndefined(s_mod) && !JS_IsNull(s_mod))
        return JS_DupValue(ctx, s_mod);
    if (s_ctx != nullptr && s_has && !JS_IsUndefined(s_mod) && !JS_IsNull(s_mod))
        JS_FreeValue(s_ctx, s_mod);
    s_ctx = ctx;
    static const char* src =
        "(function(){\n"
        "  function parseQuery(qs){ var o={}; if(!qs) return o; var p=String(qs).split('&'); for(var i=0;i<p.length;i++){ if(!p[i]) continue; var kv=p[i].split('='); var k=decodeURIComponent(kv[0]||''); if(!k) continue; var v=decodeURIComponent(kv.slice(1).join('=')||''); o[k]=v; } return o; }\n"
        "  function toPath(url){ var q=String(url||'/'); var qi=q.indexOf('?'); return qi>=0?q.slice(0,qi):q; }\n"
        "  function parseReq(req){ req.path=toPath(req.url); var qi=String(req.url||'').indexOf('?'); req.query=parseQuery(qi>=0?String(req.url).slice(qi+1):''); }\n"
        "  function normPrefix(p){ if(!p) return '/'; p=String(p); if(p[0] !== '/') p='/'+p; if(p.length>1 && p.endsWith('/')) p=p.slice(0,-1); return p; }\n"
        "  function prefixMatch(path,pfx){ if(pfx==='/'||!pfx) return true; return path===pfx || path.indexOf(pfx + '/')===0; }\n"
        "  function createApp(){\n"
        "    var app=function(req,res,next){ return app.handle(req,res,next); };\n"
        "    app._middlewares=[]; app._routes=[]; app._errors=[];\n"
        "    app.use=function(a,b){ var p='/', fn=a; if(typeof a==='string'){ p=normPrefix(a); fn=b; } if(typeof fn==='function'){ if(fn.length===4) app._errors.push({p:p,fn:fn}); else app._middlewares.push({p:p,fn:fn}); } return app; };\n"
        "    function addRoute(m,path,fn){ app._routes.push({m:m,p:normPrefix(path||'/'),fn:fn}); return app; }\n"
        "    app.get=function(path,fn){ return addRoute('GET',path,fn); };\n"
        "    app.post=function(path,fn){ return addRoute('POST',path,fn); };\n"
        "    app.put=function(path,fn){ return addRoute('PUT',path,fn); };\n"
        "    app.patch=function(path,fn){ return addRoute('PATCH',path,fn); };\n"
        "    app.delete=function(path,fn){ return addRoute('DELETE',path,fn); };\n"
        "    app.options=function(path,fn){ return addRoute('OPTIONS',path,fn); };\n"
        "    app.head=function(path,fn){ return addRoute('HEAD',path,fn); };\n"
        "    app.handle=function(req,res,out){ req.url=String(req.url||'/'); parseReq(req); var i=0;\n"
        "      function final(err){ if(err){ res.statusCode=500; return res.end(String(err&&err.message||err)); } if(typeof out==='function') return out(); res.statusCode=404; return res.end('Cannot '+(req.method||'GET')+' '+(req.path||'/')); }\n"
        "      function runError(err,ei){ if(ei>=app._errors.length) return final(err); var e=app._errors[ei]; if(!prefixMatch(req.path,e.p)) return runError(err,ei+1); return e.fn(err,req,res,function(nerr){ runError(nerr||err,ei+1); }); }\n"
        "      function runRoute(){ var m=String(req.method||'GET').toUpperCase(); for(var r=0;r<app._routes.length;r++){ var rt=app._routes[r]; if(!rt) continue; if(rt.m!==m && !(m==='HEAD'&&rt.m==='GET')) continue; if(rt.p!==req.path) continue; return rt.fn(req,res,next); } return final(); }\n"
        "      function runMw(ent){ if(!prefixMatch(req.path,ent.p)) return next(); var prev=req.url, prevPath=req.path; if(ent.p!=='/' && req.url.indexOf(ent.p)===0){ req.url=req.url.slice(ent.p.length) || '/'; parseReq(req); } return ent.fn(req,res,function(err){ req.url=prev; req.path=prevPath; if(err) return next(err); return next(); }); }\n"
        "      function next(err){ if(err) return runError(err,0); if(i<app._middlewares.length) return runMw(app._middlewares[i++]); return runRoute(); }\n"
        "      return next(); };\n"
        "    app.listen=function(port,host,cb){ if(typeof host==='function'){ cb=host; host=undefined; } var s=require('http').createServer(app); s.listen(port,host,cb); return s; };\n"
        "    return app; }\n"
        "  function express(){ return createApp(); }\n"
        "  express.Router=function(){ return createApp(); };\n"
        "  express.json=function(){ return function(req,_res,next){ var b=String(req.rawBody||'').trim(); if(!b){ req.body=req.body||{}; return next(); } if(b[0]==='{'||b[0]==='['){ try{ req.body=JSON.parse(b); }catch(e){ return next(e); } } else { req.body={}; } return next(); }; };\n"
        "  express.urlencoded=function(){ return function(req,_res,next){ var ct=String((req.headers&&req.headers['content-type'])||'').toLowerCase(); var b=String(req.rawBody||''); if(ct.indexOf('application/x-www-form-urlencoded')>=0 || b.indexOf('=')>=0){ req.body=parseQuery(b); } else { req.body=req.body||{}; } return next(); }; };\n"
        "  express.static=function(){ return function(_req,_res,next){ return next(); }; };\n"
        "  return express;\n"
        "})()";
    JSValue ex = JS_Eval(ctx, src, std::strlen(src), "<blue-express-shim>",
                         JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ex))
        return qjs_undef();
    s_mod = JS_DupValue(ctx, ex);
    s_has = true;
    return ex;
}

static JSValue qjs_blue_core_module(JSContext* ctx, const char* spec) {
    if (!spec || !*spec)
        return qjs_undef();
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue reg = JS_GetPropertyStr(ctx, g, "__blue_node_core");
    JS_FreeValue(ctx, g);
    if (JS_IsException(reg) || JS_IsUndefined(reg) || JS_IsNull(reg)) {
        JS_FreeValue(ctx, reg);
        return qjs_undef();
    }
    JSValue mod = JS_GetPropertyStr(ctx, reg, spec);
    JS_FreeValue(ctx, reg);
    if (JS_IsException(mod) || JS_IsUndefined(mod) || JS_IsNull(mod)) {
        JS_FreeValue(ctx, mod);
        return qjs_undef();
    }
    return mod;
}

static JSValue qjs_require(JSContext* ctx, JSValueConst this_val, int argc,
                           JSValueConst* argv) {
    (void)this_val;
    if (argc < 1)
        return qjs_undef();
    const char* spec = JS_ToCString(ctx, argv[0]);
    if (!spec)
        return qjs_undef();
    JSValue mod = qjs_undef();
    const char* use = spec;
    if (std::strncmp(spec, "node:", 5) == 0)
        use = spec + 5;
    mod = qjs_blue_core_module(ctx, use);
    if (!JS_IsUndefined(mod)) {
        JS_FreeCString(ctx, spec);
        return mod;
    }
    if (std::strcmp(spec, "express") == 0) {
        mod = qjs_express_module(ctx);
    } else if (std::strcmp(spec, "http") == 0 || std::strcmp(spec, "node:http") == 0) {
        mod = JS_NewObject(ctx);
        JS_SetPropertyStr(
            ctx, mod, "createServer",
            JS_NewCFunction(ctx, qjs_http_createServer, "createServer", 1));
        static const char* methodsJs =
            "['GET','POST','PUT','PATCH','DELETE','HEAD','OPTIONS']";
        static const char* statusJs =
            "({"
            "200:'OK',201:'Created',204:'No Content',"
            "301:'Moved Permanently',302:'Found',304:'Not Modified',"
            "400:'Bad Request',401:'Unauthorized',403:'Forbidden',404:'Not Found',"
            "405:'Method Not Allowed',409:'Conflict',413:'Payload Too Large',"
            "415:'Unsupported Media Type',429:'Too Many Requests',"
            "500:'Internal Server Error',501:'Not Implemented',503:'Service Unavailable'"
            "})";
        JSValue methods = JS_Eval(ctx, methodsJs, std::strlen(methodsJs),
                                  "<blue-http-methods>", JS_EVAL_TYPE_GLOBAL);
        JSValue status = JS_Eval(ctx, statusJs, std::strlen(statusJs),
                                 "<blue-http-status>", JS_EVAL_TYPE_GLOBAL);
        if (!JS_IsException(methods))
            JS_SetPropertyStr(ctx, mod, "METHODS", methods);
        else
            JS_FreeValue(ctx, methods);
        if (!JS_IsException(status))
            JS_SetPropertyStr(ctx, mod, "STATUS_CODES", status);
        else
            JS_FreeValue(ctx, status);
        JSValue g = JS_GetGlobalObject(ctx);
        JSValue inMsg = JS_GetPropertyStr(ctx, g, "__jsc_http_IncomingMessage");
        JSValue srvRes = JS_GetPropertyStr(ctx, g, "__jsc_http_ServerResponse");
        JS_FreeValue(ctx, g);
        if (!JS_IsException(inMsg))
            JS_SetPropertyStr(ctx, mod, "IncomingMessage", inMsg);
        else
            JS_FreeValue(ctx, inMsg);
        if (!JS_IsException(srvRes))
            JS_SetPropertyStr(ctx, mod, "ServerResponse", srvRes);
        else
            JS_FreeValue(ctx, srvRes);
    } else if (std::strcmp(spec, "buffer") == 0 ||
               std::strcmp(spec, "node:buffer") == 0) {
        JSValue g = JS_GetGlobalObject(ctx);
        JSValue b = JS_GetPropertyStr(ctx, g, "Buffer");
        JS_FreeValue(ctx, g);
        if (!JS_IsException(b) && !JS_IsUndefined(b)) {
            mod = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, mod, "Buffer", JS_DupValue(ctx, b));
            JSValue v = JS_GetPropertyStr(ctx, b, "from");
            if (!JS_IsException(v) && !JS_IsUndefined(v))
                JS_SetPropertyStr(ctx, mod, "from", v);
            else
                JS_FreeValue(ctx, v);
            v = JS_GetPropertyStr(ctx, b, "alloc");
            if (!JS_IsException(v) && !JS_IsUndefined(v))
                JS_SetPropertyStr(ctx, mod, "alloc", v);
            else
                JS_FreeValue(ctx, v);
            v = JS_GetPropertyStr(ctx, b, "isBuffer");
            if (!JS_IsException(v) && !JS_IsUndefined(v))
                JS_SetPropertyStr(ctx, mod, "isBuffer", v);
            else
                JS_FreeValue(ctx, v);
            v = JS_GetPropertyStr(ctx, b, "byteLength");
            if (!JS_IsException(v) && !JS_IsUndefined(v))
                JS_SetPropertyStr(ctx, mod, "byteLength", v);
            else
                JS_FreeValue(ctx, v);
            v = JS_GetPropertyStr(ctx, b, "alloc");
            if (!JS_IsException(v) && !JS_IsUndefined(v))
                JS_SetPropertyStr(ctx, mod, "allocUnsafe", JS_DupValue(ctx, v));
            if (!JS_IsException(v) && !JS_IsUndefined(v))
                JS_SetPropertyStr(ctx, mod, "allocUnsafeSlow", v);
            else
                JS_FreeValue(ctx, v);
        }
        JS_FreeValue(ctx, b);
    } else if (std::strcmp(spec, "events") == 0 ||
               std::strcmp(spec, "node:events") == 0) {
        static const char* eventsShim =
            "({"
            "EventEmitter:(function(){"
            " function E(){this._e={};}"
            " E.prototype.on=function(n,f){this._e=this._e||{};(this._e[n]||(this._e[n]=[])).push(f);return this;};"
            " E.prototype.addListener=E.prototype.on;"
            " E.prototype.once=function(n,f){var self=this;function w(){self.removeListener(n,w);return f.apply(this,arguments);} return this.on(n,w);};"
            " E.prototype.removeListener=function(n,f){this._e=this._e||{};var a=this._e[n]||[];this._e[n]=a.filter(function(x){return x!==f;});return this;};"
            " E.prototype.emit=function(n){this._e=this._e||{};var a=this._e[n]||[];var args=[].slice.call(arguments,1);for(var i=0;i<a.length;i++)a[i].apply(this,args);return a.length>0;};"
            " return E;"
            " })()"
            "})";
        mod = JS_Eval(ctx, eventsShim, std::strlen(eventsShim), "<blue-events-shim>",
                      JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(mod))
            mod = qjs_undef();
    } else if (std::strcmp(spec, "util") == 0 ||
               std::strcmp(spec, "node:util") == 0) {
        static const char* utilShim =
            "({"
            "format:function(x){return String(x);},"
            "inspect:function(x){return String(x);},"
            "deprecate:function(fn){return fn;},"
            "inherits:function(ctor,superCtor){"
            " if(!ctor||!superCtor)return;"
            " ctor.super_=superCtor;"
            " ctor.prototype=Object.create(superCtor.prototype||{});"
            " ctor.prototype.constructor=ctor;"
            "}"
            "})";
        mod = JS_Eval(ctx, utilShim, std::strlen(utilShim), "<blue-util-shim>",
                      JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(mod))
            mod = qjs_undef();
    } else if (std::strcmp(spec, "string_decoder") == 0 ||
               std::strcmp(spec, "node:string_decoder") == 0) {
        static const char* sdShim =
            "({"
            "StringDecoder:(function(){"
            " function SD(enc){this.encoding=enc||'utf8';}"
            " SD.prototype.write=function(chunk){"
            "  if(chunk==null) return '';"
            "  if(typeof chunk==='string') return chunk;"
            "  if(chunk instanceof Uint8Array){"
            "    var s=''; for(var i=0;i<chunk.length;i++) s+=String.fromCharCode(chunk[i]&255); return s;"
            "  }"
            "  return String(chunk);"
            " };"
            " SD.prototype.end=function(chunk){ return this.write(chunk); };"
            " return SD;"
            "})()"
            "})";
        mod = JS_Eval(ctx, sdShim, std::strlen(sdShim), "<blue-string-decoder-shim>",
                      JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(mod))
            mod = qjs_undef();
    } else {
        mod = qjs_from_jsnode_builtin(ctx, use);
        if (JS_IsUndefined(mod)) {
            fprintf(stderr, "blue: control plane: unsupported require('%s')\n",
                    spec);
            mod = qjs_undef();
        }
    }
    JS_FreeCString(ctx, spec);
    return mod;
}

static JSValue qjs_native_res_end(JSContext* ctx, JSValueConst this_val,
                                  int argc, JSValueConst* argv) {
    JSValue j = JS_GetPropertyStr(ctx, this_val, "__conn");
    int       ci = -1;
    if (!JS_IsException(j)) {
        int32_t x = 0;
        if (JS_ToInt32(ctx, &x, j) == 0)
            ci = (int)x;
        JS_FreeValue(ctx, j);
    }
    if (ci < 0)
        return qjs_undef();
    const char* body = "";
    char*       owned = nullptr;
    int32_t     statusCode = 200;
    std::string headersText;
    if (argc >= 1) {
        owned = (char*)JS_ToCString(ctx, argv[0]);
        if (owned)
            body = owned;
    }
    if (argc >= 2) {
        (void)JS_ToInt32(ctx, &statusCode, argv[1]);
        if (statusCode < 100 || statusCode > 999)
            statusCode = 200;
    }
    if (argc >= 3 && JS_IsObject(argv[2])) {
        JSPropertyEnum* props = nullptr;
        uint32_t propCount = 0;
        if (JS_GetOwnPropertyNames(ctx, &props, &propCount, argv[2],
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            for (uint32_t i = 0; i < propCount; ++i) {
                JSValue keyVal = JS_AtomToString(ctx, props[i].atom);
                const char* key = JS_ToCString(ctx, keyVal);
                JSValue val = JS_GetProperty(ctx, argv[2], props[i].atom);
                const char* value = JS_ToCString(ctx, val);
                if (key && value && *key) {
                    headersText += key;
                    headersText += ": ";
                    headersText += value;
                    headersText += "\r\n";
                }
                if (value)
                    JS_FreeCString(ctx, value);
                if (key)
                    JS_FreeCString(ctx, key);
                JS_FreeValue(ctx, val);
                JS_FreeValue(ctx, keyVal);
                JS_FreeAtom(ctx, props[i].atom);
            }
            js_free(ctx, props);
        }
    }
    jsc_http_conn_res_send_cstr(
        ci, statusCode, headersText.empty() ? nullptr : headersText.c_str(), body);
    if (owned)
        JS_FreeCString(ctx, owned);
    return qjs_undef();
}

/* Binary-safe variant: extracts raw bytes from a Uint8Array/ArrayBuffer
 * and sends them without any string coercion or null-byte truncation. */
static JSValue qjs_native_res_binary_end(JSContext* ctx, JSValueConst this_val,
                                         int argc, JSValueConst* argv) {
    JSValue j = JS_GetPropertyStr(ctx, this_val, "__conn");
    int ci = -1;
    if (!JS_IsException(j)) {
        int32_t x = 0;
        if (JS_ToInt32(ctx, &x, j) == 0)
            ci = (int)x;
        JS_FreeValue(ctx, j);
    }
    if (ci < 0)
        return qjs_undef();

    const uint8_t* body_data = nullptr;
    size_t body_len = 0;
    int32_t statusCode = 200;
    std::string headersText;

    /* argv[0]: Uint8Array body */
    if (argc >= 1) {
        size_t byte_offset = 0, byte_length = 0, bytes_per_element = 0;
        JSValue ab = JS_GetTypedArrayBuffer(ctx, argv[0], &byte_offset,
                                            &byte_length, &bytes_per_element);
        if (!JS_IsException(ab)) {
            size_t ab_size = 0;
            body_data = JS_GetArrayBuffer(ctx, &ab_size, ab);
            fprintf(stderr, "DEBUG binary_end: ab_size=%zu byte_offset=%zu byte_length=%zu bpe=%zu data=%p\n",
                    ab_size, byte_offset, byte_length, bytes_per_element, (void*)body_data);
            if (body_data) {
                body_data += byte_offset;
                body_len = byte_length;
            }
            JS_FreeValue(ctx, ab);
        } else {
            fprintf(stderr, "DEBUG binary_end: JS_GetTypedArrayBuffer FAILED (not a typed array?)\n");
        }
    }
    /* argv[1]: statusCode */
    if (argc >= 2) {
        (void)JS_ToInt32(ctx, &statusCode, argv[1]);
        if (statusCode < 100 || statusCode > 999)
            statusCode = 200;
    }
    /* argv[2]: headers object */
    if (argc >= 3 && JS_IsObject(argv[2])) {
        JSPropertyEnum* props = nullptr;
        uint32_t propCount = 0;
        if (JS_GetOwnPropertyNames(ctx, &props, &propCount, argv[2],
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            for (uint32_t i = 0; i < propCount; ++i) {
                JSValue keyVal = JS_AtomToString(ctx, props[i].atom);
                const char* key = JS_ToCString(ctx, keyVal);
                JSValue val = JS_GetProperty(ctx, argv[2], props[i].atom);
                const char* value = JS_ToCString(ctx, val);
                if (key && value && *key) {
                    headersText += key;
                    headersText += ": ";
                    headersText += value;
                    headersText += "\r\n";
                }
                if (value)
                    JS_FreeCString(ctx, value);
                if (key)
                    JS_FreeCString(ctx, key);
                JS_FreeValue(ctx, val);
                JS_FreeValue(ctx, keyVal);
                JS_FreeAtom(ctx, props[i].atom);
            }
            js_free(ctx, props);
        }
    }
    jsc_http_conn_res_send_binary(
        ci, statusCode, headersText.empty() ? nullptr : headersText.c_str(),
        body_data, body_len);
    return qjs_undef();
}

extern "C" void jsc_qjs_dispatch_http_conn(void* opaque, const char* method,
                                           const char* path, const char* body,
                                           const char* headers_text,
                                           int conn_index) {
    static thread_local int s_qjs_http_dispatch_depth;
    if (s_qjs_http_dispatch_depth > 0) {
        fprintf(stderr,
                "blue: nested QuickJS HTTP dispatch (depth=%d) %s %s\n",
                s_qjs_http_dispatch_depth, method ? method : "?",
                path ? path : "?");
        fflush(stderr);
        jsc_http_conn_res_send_cstr(conn_index, 500,
                                    "content-type: application/json; charset=utf-8\r\n",
                                    "{\"error\":\"nested_http_dispatch\"}");
        return;
    }
    ++s_qjs_http_dispatch_depth;

    auto* qh = (QjsHttpHandler*)opaque;
    if (!qh || !qh->ctx) {
        --s_qjs_http_dispatch_depth;
        return;
    }
    JSContext* ctx = qh->ctx;

    JSValue req = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, req, "method", JS_NewString(ctx, method));
    JS_SetPropertyStr(ctx, req, "url", JS_NewString(ctx, path));
    JS_SetPropertyStr(ctx, req, "rawBody",
                      JS_NewString(ctx, body ? body : ""));
    JS_SetPropertyStr(ctx, req, "httpVersionMajor", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, req, "httpVersionMinor", JS_NewInt32(ctx, 1));

    JSValue res = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, res, "__conn", JS_NewInt32(ctx, conn_index));
    JS_SetPropertyStr(ctx, res, "__nativeEnd",
                      JS_NewCFunction(ctx, qjs_native_res_end, "end", 3));
    JS_SetPropertyStr(ctx, res, "__nativeBinaryEnd",
                      JS_NewCFunction(ctx, qjs_native_res_binary_end, "binaryEnd", 3));

    JSValue g = JS_GetGlobalObject(ctx);
    JSValue patchFn = JS_GetPropertyStr(ctx, g, "__jsc_http_patch");
    JS_FreeValue(ctx, g);
    if (!JS_IsException(patchFn) && JS_IsFunction(ctx, patchFn)) {
        JSValue m = JS_NewString(ctx, method ? method : "GET");
        JSValue u = JS_NewString(ctx, path ? path : "/");
        JSValue b = JS_NewString(ctx, body ? body : "");
        JSValue h = JS_NewString(ctx, headers_text ? headers_text : "");
        JSValueConst pArgs[6] = {req, res, m, u, b, h};
        JSValue patched = JS_Call(ctx, patchFn, qjs_undef(), 6, pArgs);
        JS_FreeValue(ctx, m);
        JS_FreeValue(ctx, u);
        JS_FreeValue(ctx, b);
        JS_FreeValue(ctx, h);
        if (JS_IsException(patched)) {
            JSValue ex = JS_GetException(ctx);
            const char* msg = JS_ToCString(ctx, ex);
            fprintf(stderr, "blue: http patch error: %s\n", msg ? msg : "?");
            if (msg)
                JS_FreeCString(ctx, msg);
            JS_FreeValue(ctx, ex);
        }
        JS_FreeValue(ctx, patched);
    }
    JS_FreeValue(ctx, patchFn);

    JSValueConst args[2] = {req, res};
    JSValue      ret = JS_Call(ctx, qh->fn, qjs_undef(), 2, args);
    if (JS_IsException(ret)) {
        JSValue ex = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, ex);
        fprintf(stderr, "blue: QuickJS http handler error: %s\n",
                msg ? msg : "?");
        JSValue st = JS_GetPropertyStr(ctx, ex, "stack");
        if (!JS_IsException(st) && !JS_IsUndefined(st)) {
            const char* s = JS_ToCString(ctx, st);
            if (s) {
                fprintf(stderr, "blue: QuickJS http handler stack: %s\n", s);
                JS_FreeCString(ctx, s);
            }
            JS_FreeValue(ctx, st);
        } else if (!JS_IsException(st)) {
            JS_FreeValue(ctx, st);
        }
        if (msg)
            JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, ex);
    }
    JS_FreeValue(ctx, ret);
    /* Drain microtasks before freeing req/res: Express may schedule work that
     * still reads the request (e.g. parseurl / routing) after the handler
     * returns but before the job queue runs. */
    jsc_quickjs_pump_microtasks();
    JS_FreeValue(ctx, req);
    JS_FreeValue(ctx, res);
    --s_qjs_http_dispatch_depth;
}

extern "C" void jsc_qjs_install_control_plane_shims(JSContext* ctx) {
    static const char* prologue =
        "if (typeof Error !== 'undefined') {\n"
        "  if (typeof Error.stackTraceLimit === 'undefined') Error.stackTraceLimit = 10;\n"
        "  if (typeof Error.prepareStackTrace === 'undefined') Error.prepareStackTrace = void 0;\n"
        "  if (typeof Error.captureStackTrace !== 'function') {\n"
        "    Error.captureStackTrace = function(obj){\n"
        "      if (!obj) return;\n"
        "      var fakeSite = {\n"
        "        getFileName: function(){ return 'blue-hybrid-island.js'; },\n"
        "        getFunctionName: function(){ return ''; },\n"
        "        getFunction: function(){ return null; },\n"
        "        getTypeName: function(){ return ''; },\n"
        "        getMethodName: function(){ return ''; },\n"
        "        getLineNumber: function(){ return 1; },\n"
        "        getColumnNumber: function(){ return 1; },\n"
        "        isEval: function(){ return false; },\n"
        "        isNative: function(){ return false; },\n"
        "        isToplevel: function(){ return true; },\n"
        "        toString: function(){ return 'blue-hybrid-island.js:1:1'; }\n"
        "      };\n"
        "      obj.stack = [fakeSite, fakeSite, fakeSite];\n"
        "    };\n"
        "  }\n"
        "}\n"
        "if (typeof globalThis.setImmediate !== 'function') {\n"
        "  var __jsc_si_id = 1;\n"
        "  var __jsc_si_live = {};\n"
        "  globalThis.setImmediate = function(cb){\n"
        "    var id = __jsc_si_id++;\n"
        "    __jsc_si_live[id] = 1;\n"
        "    Promise.resolve().then(function(){\n"
        "      if (!__jsc_si_live[id]) return;\n"
        "      delete __jsc_si_live[id];\n"
        "      if (typeof cb === 'function') cb();\n"
        "    });\n"
        "    return id;\n"
        "  };\n"
        "}\n"
        "if (typeof globalThis.clearImmediate !== 'function') {\n"
        "  globalThis.clearImmediate = function(id){\n"
        "    if (id != null) delete __jsc_si_live[id];\n"
        "  };\n"
        "}\n"
        "if (typeof globalThis.setTimeout !== 'function') {\n"
        "  var __jsc_to_id = 1;\n"
        "  var __jsc_to_live = {};\n"
        "  globalThis.setTimeout = function(cb){\n"
        "    var args=[].slice.call(arguments,2), id=__jsc_to_id++;\n"
        "    __jsc_to_live[id]=1;\n"
        "    Promise.resolve().then(function(){ if(!__jsc_to_live[id]) return; delete __jsc_to_live[id]; if(typeof cb==='function') cb.apply(null,args); });\n"
        "    return id;\n"
        "  };\n"
        "}\n"
        "if (typeof globalThis.clearTimeout !== 'function') {\n"
        "  globalThis.clearTimeout = function(id){ if(id!=null) delete __jsc_to_live[id]; };\n"
        "}\n"
        "if (typeof globalThis.setInterval !== 'function') {\n"
        "  globalThis.setInterval = function(cb){ return setTimeout.apply(null, arguments); };\n"
        "}\n"
        "if (typeof globalThis.clearInterval !== 'function') {\n"
        "  globalThis.clearInterval = clearTimeout;\n"
        "}\n"
        "if (typeof globalThis.__blue_console_print_line === 'function') {\n"
        "  function __blue_fmtConsoleArgs(args){\n"
        "    var a='', i;\n"
        "    for (i=0;i<args.length;i++){ if(i) a+=' '; try{ a+=(args[i]==null?'':String(args[i])); }catch(_e){ a+='[Object]'; } }\n"
        "    return a;\n"
        "  }\n"
        "  globalThis.console = {\n"
        "    log:function(){ globalThis.__blue_console_print_line(__blue_fmtConsoleArgs(arguments)); },\n"
        "    error:function(){ globalThis.__blue_console_print_line(__blue_fmtConsoleArgs(arguments)); },\n"
        "    warn:function(){ globalThis.__blue_console_print_line(__blue_fmtConsoleArgs(arguments)); },\n"
        "    info:function(){ globalThis.__blue_console_print_line(__blue_fmtConsoleArgs(arguments)); }\n"
        "  };\n"
        "}\n";
    JSValue gPre = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(
        ctx, gPre, "__blue_console_print_line",
        JS_NewCFunction(ctx, qjs_console_print_line, "__blue_console_print_line", 1));
    JS_FreeValue(ctx, gPre);
    JSValue r = JS_Eval(ctx, prologue, std::strlen(prologue), "<blue-node-shims>",
                        JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) {
        JSValue ex = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, ex);
        fprintf(stderr, "blue: prologue error: %s\n", msg ? msg : "?");
        if (msg)
            JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, ex);
    }
    JS_FreeValue(ctx, r);

    JSValue g = JS_GetGlobalObject(ctx);
    JSValue processMod = qjs_from_jsnode_builtin(ctx, "process");
    if (!JS_IsUndefined(processMod) && !JS_IsException(processMod)) {
        JS_SetPropertyStr(ctx, g, "process", processMod);
        static const char* nextTickShim =
            "if (typeof process !== 'undefined') {\n"
            "  process.nextTick = function(cb){ var args=[].slice.call(arguments,1); Promise.resolve().then(function(){ if (typeof cb==='function') cb.apply(null,args); }); };\n"
            "  process.browser = false;\n"
            "  process.title = process.title || 'blue';\n"
            "  process.version = process.version || 'v18.0.0-blue';\n"
            "  process.versions = process.versions || { node:'18.0.0-blue', quickjs:'blue' };\n"
            "  process.release = process.release || { name:'node' };\n"
            "}\n";
        JSValue ntr = JS_Eval(ctx, nextTickShim, std::strlen(nextTickShim),
                              "<blue-nexttick-shim>", JS_EVAL_TYPE_GLOBAL);
        JS_FreeValue(ctx, ntr);
    }
    JS_SetPropertyStr(ctx, g, "global", JS_DupValue(ctx, g));
    JS_SetPropertyStr(
        ctx, g, "require",
        JS_NewCFunction(ctx, qjs_require, "require", 1));
    JS_FreeValue(ctx, g);

    static const char* bufferShim =
        "if (typeof globalThis.Buffer !== 'function') {\n"
        "  function __b64chars(){ return 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/='; }\n"
        "  function __bytesToUtf8(a){ var s='',i=0,c,c2,c3,c4,u; while(i<a.length){ c=a[i++]; if(c<128){ s+=String.fromCharCode(c); } else if(c<224){ c2=a[i++]; s+=String.fromCharCode(((c&31)<<6)|(c2&63)); } else if(c<240){ c2=a[i++]; c3=a[i++]; s+=String.fromCharCode(((c&15)<<12)|((c2&63)<<6)|(c3&63)); } else { c2=a[i++]; c3=a[i++]; c4=a[i++]; u=((c&7)<<18)|((c2&63)<<12)|((c3&63)<<6)|(c4&63); u-=0x10000; s+=String.fromCharCode(0xD800+(u>>10),0xDC00+(u&1023)); } } return s; }\n"
        "  function __utf8ToBytes(s){ s=String(s); var out=[]; for(var i=0;i<s.length;i++){ var c=s.charCodeAt(i); if(c>=0xD800&&c<=0xDBFF&&i+1<s.length){ var d=s.charCodeAt(++i); c=0x10000+((c-0xD800)<<10)+(d-0xDC00); } if(c<128) out.push(c); else if(c<2048) out.push(192|(c>>6),128|(c&63)); else if(c<65536) out.push(224|(c>>12),128|((c>>6)&63),128|(c&63)); else out.push(240|(c>>18),128|((c>>12)&63),128|((c>>6)&63),128|(c&63)); } return out; }\n"
        "  function __hexToBytes(s){ s=String(s).replace(/[^0-9a-f]/ig,''); var out=[]; for(var i=0;i+1<s.length;i+=2) out.push(parseInt(s.slice(i,i+2),16)); return out; }\n"
        "  function __bytesToHex(a){ var h='0123456789abcdef',s=''; for(var i=0;i<a.length;i++){ var b=a[i]&255; s+=h[b>>4]+h[b&15]; } return s; }\n"
        "  function __bytesToBase64(a){ var c=__b64chars(),s='',i=0; while(i<a.length){ var b1=a[i++],b2=i<a.length?a[i++]:NaN,b3=i<a.length?a[i++]:NaN; var e1=b1>>2,e2=((b1&3)<<4)|((b2||0)>>4),e3=isNaN(b2)?64:(((b2&15)<<2)|((b3||0)>>6)),e4=isNaN(b3)?64:(b3&63); s+=c.charAt(e1)+c.charAt(e2)+c.charAt(e3)+c.charAt(e4); } return s; }\n"
        "  function __base64ToBytes(input){ var c=__b64chars(),str=String(input).replace(/[^A-Za-z0-9+/=]/g,''),out=[]; for(var i=0;i<str.length;){ var e1=c.indexOf(str.charAt(i++)),e2=c.indexOf(str.charAt(i++)),e3=c.indexOf(str.charAt(i++)),e4=c.indexOf(str.charAt(i++)); if(e1<0||e2<0) break; out.push((e1<<2)|(e2>>4)); if(e3!==64&&e3>=0) out.push(((e2&15)<<4)|(e3>>2)); if(e4!==64&&e4>=0) out.push(((e3&3)<<6)|e4); } return out; }\n"
        "  function __decorateBuffer(a){ if(!a) a=new Uint8Array(0); a.toString=function(enc,start,end){ enc=String(enc||'utf8').toLowerCase(); start=start==null?0:start|0; end=end==null?a.length:end|0; var v=a.slice(start,end); if(enc==='hex') return __bytesToHex(v); if(enc==='base64') return __bytesToBase64(v); if(enc==='utf8'||enc==='utf-8'||enc==='binary'||enc==='latin1') return __bytesToUtf8(v); return __bytesToUtf8(v); }; a.equals=function(b){ if(!b||a.length!==b.length) return false; for(var i=0;i<a.length;i++) if(a[i]!==b[i]) return false; return true; }; a.copy=function(target,tstart,sstart,send){ tstart=tstart||0; sstart=sstart||0; send=send==null?a.length:send; for(var i=sstart;i<send&&tstart<target.length;i++,tstart++) target[tstart]=a[i]; return Math.max(0,send-sstart); }; a.subarray=Uint8Array.prototype.subarray.bind(a); a.slice=function(s,e){ return __decorateBuffer(Uint8Array.prototype.slice.call(a,s,e)); }; return a; }\n"
        "  function Buffer(arg, enc) { return Buffer.from(arg, enc); }\n"
        "  Buffer.from = function(arg, enc) {\n"
        "    if (arg == null) return new Uint8Array(0);\n"
        "    if (arg instanceof ArrayBuffer) return __decorateBuffer(new Uint8Array(arg));\n"
        "    if (arg instanceof Uint8Array) return __decorateBuffer(new Uint8Array(arg));\n"
        "    if (Array.isArray(arg)) return __decorateBuffer(new Uint8Array(arg));\n"
        "    enc=String(enc||'utf8').toLowerCase(); var bytes;\n"
        "    if(enc==='hex') bytes=__hexToBytes(arg); else if(enc==='base64') bytes=__base64ToBytes(arg); else bytes=__utf8ToBytes(arg);\n"
        "    return __decorateBuffer(new Uint8Array(bytes));\n"
        "  };\n"
        "  Buffer.alloc = function(n, fill) {\n"
        "    n = n|0; if (n < 0) n = 0; var a=__decorateBuffer(new Uint8Array(n)); if(fill!=null) a.fill(typeof fill==='number'?fill:Buffer.from(fill)[0]||0); return a;\n"
        "  };\n"
        "  Buffer.allocUnsafe = Buffer.alloc; Buffer.allocUnsafeSlow = Buffer.alloc;\n"
        "  Buffer.byteLength = function(v,enc) { return Buffer.from(v,enc).length; };\n"
        "  Buffer.concat = function(list,len){ if(!Array.isArray(list)) throw new TypeError('list must be Array'); if(len==null){ len=0; for(var i=0;i<list.length;i++) len+=list[i].length||0; } var out=Buffer.alloc(len),off=0; for(var i=0;i<list.length;i++){ var b=Buffer.from(list[i]); out.set(b.slice(0,Math.max(0,len-off)),off); off+=b.length; if(off>=len) break; } return out; };\n"
        "  Buffer.compare = function(a,b){ a=Buffer.from(a); b=Buffer.from(b); for(var i=0;i<Math.min(a.length,b.length);i++){ if(a[i]!==b[i]) return a[i]<b[i]?-1:1; } return a.length===b.length?0:(a.length<b.length?-1:1); };\n"
        "  Buffer.isBuffer = function(v) { return v instanceof Uint8Array; };\n"
        "  Buffer.prototype = Uint8Array.prototype;\n"
        "  if(!Uint8Array.prototype.toString || String(new Uint8Array([65]))!=='A') Uint8Array.prototype.toString=function(enc,start,end){ return __decorateBuffer(this).toString(enc,start,end); };\n"
        "  globalThis.Buffer = Buffer;\n"
        "  if(typeof globalThis.TextEncoder!=='function') globalThis.TextEncoder=function(){};\n"
        "  if(globalThis.TextEncoder && !globalThis.TextEncoder.prototype.encode) globalThis.TextEncoder.prototype.encode=function(s){ return Buffer.from(String(s)); };\n"
        "  if(typeof globalThis.TextDecoder!=='function') globalThis.TextDecoder=function(enc){ this.encoding=enc||'utf-8'; };\n"
        "  if(globalThis.TextDecoder && !globalThis.TextDecoder.prototype.decode) globalThis.TextDecoder.prototype.decode=function(v){ return Buffer.from(v||[]).toString(this.encoding); };\n"
        "  if(typeof globalThis.btoa!=='function') globalThis.btoa=function(s){ return __bytesToBase64(Buffer.from(String(s),'binary')); };\n"
        "  if(typeof globalThis.atob!=='function') globalThis.atob=function(s){ return Buffer.from(String(s),'base64').toString('binary'); };\n"
        "}\n";
    JSValue b = JS_Eval(ctx, bufferShim, std::strlen(bufferShim),
                        "<blue-buffer-shim>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(b)) {
        JSValue ex = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, ex);
        fprintf(stderr, "blue: buffer shim error: %s\n", msg ? msg : "?");
        if (msg)
            JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, ex);
    }
    JS_FreeValue(ctx, b);

    static const char* coreCompatShim =
        "if (!globalThis.__blue_node_core) {\n"
        "  var __blue_node_core = {};\n"
        "  function __EE(){ this._events = {}; }\n"
        "  __EE.prototype.on=function(n,f){ (this._events[n]||(this._events[n]=[])).push(f); return this; };\n"
        "  __EE.prototype.addListener=__EE.prototype.on;\n"
        "  __EE.prototype.prependListener=function(n,f){ (this._events[n]||(this._events[n]=[])).unshift(f); return this; };\n"
        "  __EE.prototype.once=function(n,f){ var self=this; function w(){ self.removeListener(n,w); return f.apply(this,arguments); } w.listener=f; return this.on(n,w); };\n"
        "  __EE.prototype.removeListener=function(n,f){ var a=this._events[n]||[]; this._events[n]=a.filter(function(x){ return x!==f && x.listener!==f; }); return this; };\n"
        "  __EE.prototype.off=__EE.prototype.removeListener;\n"
        "  __EE.prototype.removeAllListeners=function(n){ if(n==null) this._events={}; else delete this._events[n]; return this; };\n"
        "  __EE.prototype.listeners=function(n){ return (this._events[n]||[]).slice(); };\n"
        "  __EE.prototype.listenerCount=function(n){ return (this._events[n]||[]).length; };\n"
        "  __EE.prototype.eventNames=function(){ return Object.keys(this._events); };\n"
        "  __EE.prototype.emit=function(n){ var a=(this._events[n]||[]).slice(), args=[].slice.call(arguments,1); if(n==='error'&&a.length===0) throw args[0]||new Error('Unhandled error event'); for(var i=0;i<a.length;i++) a[i].apply(this,args); return a.length>0; };\n"
        "  __blue_node_core.events = { EventEmitter: __EE, once:function(ee,n){ return new Promise(function(res){ ee.once(n,function(){ res([].slice.call(arguments)); }); }); } };\n"
        "  __blue_node_core.events.default = __EE;\n"
        "  function __norm(p){ p=String(p||''); var abs=p.charAt(0)==='/', parts=p.split('/'), out=[]; for(var i=0;i<parts.length;i++){ var x=parts[i]; if(!x||x==='.') continue; if(x==='..'){ if(out.length&&out[out.length-1]!=='..') out.pop(); else if(!abs) out.push('..'); } else out.push(x); } return (abs?'/':'')+out.join('/') || (abs?'/':'.'); }\n"
        "  var path={ sep:'/', delimiter:':', normalize:__norm, isAbsolute:function(p){ return String(p||'').charAt(0)==='/'; }, join:function(){ return __norm([].slice.call(arguments).filter(function(x){return x!=='';}).join('/')); }, resolve:function(){ var out=''; for(var i=arguments.length-1;i>=0;i--){ var p=String(arguments[i]||''); if(!p) continue; out=p+'/'+out; if(p.charAt(0)==='/') break; } if(out.charAt(0)!=='/') out=(process&&process.cwd?process.cwd():'/')+'/'+out; return __norm(out); }, dirname:function(p){ p=__norm(p); if(p==='/') return '/'; var i=p.lastIndexOf('/'); return i<0?'.':(i===0?'/':p.slice(0,i)); }, basename:function(p,ext){ p=String(p||'').replace(/\\/+$/,''); var b=p.slice(p.lastIndexOf('/')+1); if(ext&&b.endsWith(ext)) b=b.slice(0,-String(ext).length); return b; }, extname:function(p){ var b=path.basename(p), i=b.lastIndexOf('.'); return i<=0?'':b.slice(i); }, parse:function(p){ var dir=path.dirname(p), base=path.basename(p), ext=path.extname(base); return {root:path.isAbsolute(p)?'/':'',dir:dir,base:base,ext:ext,name:ext?base.slice(0,-ext.length):base}; }, format:function(o){ if(!o) return ''; var dir=o.dir||o.root||'', base=o.base || ((o.name||'')+(o.ext||'')); return dir ? (dir==='/'?'/'+base:dir+'/'+base) : base; }, relative:function(from,to){ from=__norm(from).split('/').filter(Boolean); to=__norm(to).split('/').filter(Boolean); while(from.length&&to.length&&from[0]===to[0]){from.shift();to.shift();} return from.map(function(){return '..';}).concat(to).join('/')||''; } };\n"
        "  path.posix=path; __blue_node_core.path=path;\n"
        "  function AssertionError(o){ this.name='AssertionError'; this.message=(o&&o.message)||'Assertion failed'; this.actual=o&&o.actual; this.expected=o&&o.expected; this.operator=o&&o.operator; if(Error.captureStackTrace) Error.captureStackTrace(this); else this.stack=this.message; } AssertionError.prototype=Object.create(Error.prototype); AssertionError.prototype.constructor=AssertionError;\n"
        "  function fail(a,e,m,op){ throw new AssertionError({actual:a,expected:e,message:m||('Expected '+a+' '+op+' '+e),operator:op}); }\n"
        "  function deep(a,b){ if(a===b) return true; if(typeof a!==typeof b) return false; if(!a||!b||typeof a!=='object') return false; var ak=Object.keys(a), bk=Object.keys(b); if(ak.length!==bk.length) return false; ak.sort(); bk.sort(); for(var i=0;i<ak.length;i++){ if(ak[i]!==bk[i]||!deep(a[ak[i]],b[bk[i]])) return false; } return true; }\n"
        "  function assert(v,m){ if(!v) fail(v,true,m,'=='); }\n"
        "  assert.ok=assert; assert.AssertionError=AssertionError; assert.fail=function(m){ throw new AssertionError({message:m||'Failed'}); }; assert.equal=function(a,b,m){ if(a!=b) fail(a,b,m,'=='); }; assert.notEqual=function(a,b,m){ if(a==b) fail(a,b,m,'!='); }; assert.strictEqual=function(a,b,m){ if(a!==b) fail(a,b,m,'==='); }; assert.notStrictEqual=function(a,b,m){ if(a===b) fail(a,b,m,'!=='); }; assert.deepEqual=assert.deepStrictEqual=function(a,b,m){ if(!deep(a,b)) fail(a,b,m,'deepEqual'); }; assert.notDeepEqual=assert.notDeepStrictEqual=function(a,b,m){ if(deep(a,b)) fail(a,b,m,'notDeepEqual'); }; assert.throws=function(fn,re,m){ var threw=false,err; try{fn();}catch(e){threw=true;err=e;} if(!threw) fail(false,true,m||'Missing expected exception','throws'); if(re&&re.test&&!re.test(String(err&&err.message||err))) fail(err,re,m,'throws'); }; assert.doesNotThrow=function(fn,m){ try{fn();}catch(e){ fail(e,null,m||'Got unwanted exception','doesNotThrow'); } };\n"
        "  __blue_node_core.assert=assert;\n"
        "  __blue_node_core['assert/strict']=assert;\n"
        "  __blue_node_core.querystring={ parse:function(s,sep,eq){ sep=sep||'&'; eq=eq||'='; var o={}; if(!s) return o; String(s).split(sep).forEach(function(p){ if(!p) return; var i=p.indexOf(eq), k=i>=0?p.slice(0,i):p, v=i>=0?p.slice(i+eq.length):''; k=decodeURIComponent(k.replace(/\\+/g,' ')); v=decodeURIComponent(v.replace(/\\+/g,' ')); if(o[k]===undefined)o[k]=v; else if(Array.isArray(o[k]))o[k].push(v); else o[k]=[o[k],v]; }); return o; }, stringify:function(o,sep,eq){ sep=sep||'&'; eq=eq||'='; var a=[]; for(var k in (o||{})){ var v=o[k]; if(Array.isArray(v)){ for(var i=0;i<v.length;i++) a.push(encodeURIComponent(k)+eq+encodeURIComponent(v[i])); } else a.push(encodeURIComponent(k)+eq+encodeURIComponent(v)); } return a.join(sep); }, escape:encodeURIComponent, unescape:decodeURIComponent };\n"
        "  __blue_node_core.url={ URL: globalThis.URL, URLSearchParams: globalThis.URLSearchParams, parse:function(u,q){ var s=String(u||''), out={href:s}; var rest=s, hash='', search='', query='', protocol='', host='', hostname='', port='', pathname=''; var hi=rest.indexOf('#'); if(hi>=0){ hash=rest.slice(hi); rest=rest.slice(0,hi); } var qi=rest.indexOf('?'); if(qi>=0){ search=rest.slice(qi); query=rest.slice(qi+1); rest=rest.slice(0,qi); } var pi=rest.match(/^([a-zA-Z][a-zA-Z0-9+.-]*:)\\/\\//); if(pi){ protocol=pi[1]; rest=rest.slice(pi[0].length); var si=rest.indexOf('/'); host=si>=0?rest.slice(0,si):rest; pathname=si>=0?rest.slice(si):'/'; var ci=host.lastIndexOf(':'); if(ci>0){ hostname=host.slice(0,ci); port=host.slice(ci+1); } else hostname=host; } else { pathname=rest||'/'; } out.protocol=protocol; out.slashes=!!protocol; out.host=host; out.hostname=hostname; out.port=port; out.pathname=pathname||'/'; out.path=(pathname||'/')+search; out.query=q?__blue_node_core.querystring.parse(query):query; out.hash=hash; out.search=search; return out; }, format:function(o){ if(typeof o==='string') return o; if(!o) return ''; return (o.protocol||'')+(o.slashes?'//':'')+(o.host||'')+(o.pathname||'')+(o.search||'')+(o.hash||''); }, resolve:function(a,b){ try{return globalThis.URL?new URL(b,a).toString():String(b||'');}catch(e){return String(b||'');} }, domainToASCII:function(s){return String(s||'');}, domainToUnicode:function(s){return String(s||'');} };\n"
        "  __blue_node_core.timers={ setTimeout:setTimeout, clearTimeout:clearTimeout, setInterval:setInterval, clearInterval:clearInterval, setImmediate:setImmediate, clearImmediate:clearImmediate };\n"
        "  __blue_node_core['timers/promises']={ setTimeout:function(ms,v){ return new Promise(function(res){ setTimeout(function(){res(v);},ms||0); }); }, setImmediate:function(v){ return new Promise(function(res){ setImmediate(function(){res(v);}); }); } };\n"
        "  __blue_node_core.os={ platform:function(){ return process&&process.platform||'linux'; }, arch:function(){ return process&&process.arch||'x64'; }, homedir:function(){ return process&&process.env&&(process.env.HOME||process.env.USERPROFILE)||''; }, tmpdir:function(){ return process&&process.env&&(process.env.TMPDIR||process.env.TEMP)||'/tmp'; }, EOL:'\\n', endianness:function(){return 'LE';}, cpus:function(){return [];}, totalmem:function(){return 0;}, freemem:function(){return 0;}, hostname:function(){return 'blue';}, type:function(){return 'Blue';}, release:function(){return '0';} };\n"
        "  __blue_node_core.util={ format:function(){ var i=0,args=[].slice.call(arguments),f=String(args.shift()||''); return f.replace(/%[sdjifoO%]/g,function(x){ if(x==='%%') return '%'; var v=args[i++]; if(x==='%j'){ try{return JSON.stringify(v);}catch(e){return '[Circular]';} } return String(v); })+(args.length>i?' '+args.slice(i).map(String).join(' '):''); }, inspect:function(v){ try{return typeof v==='string'?v:JSON.stringify(v);}catch(e){return String(v);} }, inherits:function(c,s){ c.super_=s; c.prototype=Object.create((s&&s.prototype)||{}); c.prototype.constructor=c; }, deprecate:function(fn){return fn;}, promisify:function(fn){ return function(){ var self=this,args=[].slice.call(arguments); return new Promise(function(res,rej){ args.push(function(err,val){ if(err) rej(err); else res(val); }); fn.apply(self,args); }); }; }, callbackify:function(fn){ return function(){ var args=[].slice.call(arguments), cb=args.pop(); Promise.resolve(fn.apply(this,args)).then(function(v){cb(null,v);},cb); }; }, types:{ isDate:function(v){return v instanceof Date;}, isRegExp:function(v){return v instanceof RegExp;}, isPromise:function(v){return !!v&&typeof v.then==='function';}, isArrayBuffer:function(v){return v instanceof ArrayBuffer;}, isTypedArray:function(v){return v instanceof Uint8Array;} } };\n"
        "  __blue_node_core['util/types']=__blue_node_core.util.types;\n"
        "  function Readable(){ __EE.call(this); } Readable.prototype=Object.create(__EE.prototype); Readable.prototype.constructor=Readable; Readable.from=function(iter){ var r=new Readable(); setImmediate(function(){ try{ for(var i of iter) r.emit('data',i); }catch(e){ r.emit('error',e); } r.emit('end'); }); return r; }; Readable.prototype.pipe=function(w){ this.on('data',function(c){ if(w&&w.write) w.write(c); }); this.on('end',function(){ if(w&&w.end) w.end(); }); return w; };\n"
        "  function Writable(){ __EE.call(this); } Writable.prototype=Object.create(__EE.prototype); Writable.prototype.constructor=Writable; Writable.prototype.write=function(c,cb){ if(cb) cb(); this.emit('drain'); return true; }; Writable.prototype.end=function(c,cb){ if(c!=null) this.write(c); if(cb) cb(); this.emit('finish'); return this; };\n"
        "  function PassThrough(){ Readable.call(this); } PassThrough.prototype=Object.create(Readable.prototype); PassThrough.prototype.constructor=PassThrough; PassThrough.prototype.write=function(c){ this.emit('data',c); return true; }; PassThrough.prototype.end=function(c){ if(c!=null)this.write(c); this.emit('end'); return this; };\n"
        "  __blue_node_core.stream={ Stream:__EE, Readable:Readable, Writable:Writable, Duplex:Readable, Transform:PassThrough, PassThrough:PassThrough, pipeline:function(){ var cb=arguments[arguments.length-1]; if(typeof cb==='function') cb(); }, finished:function(_s,cb){ if(cb) setImmediate(cb); } };\n"
        "  __blue_node_core['stream/promises']={ pipeline:function(){ return Promise.resolve(); }, finished:function(){ return Promise.resolve(); } };\n"
        "  __blue_node_core['stream/web']={ ReadableStream:globalThis.ReadableStream, WritableStream:globalThis.WritableStream, TransformStream:globalThis.TransformStream };\n"
        "  __blue_node_core.constants={};\n"
        "  var __builtinModules=['assert','assert/strict','buffer','child_process','constants','crypto','dns','events','fs','fs/promises','http','https','module','os','path','perf_hooks','process','querystring','readline','stream','stream/promises','string_decoder','timers','timers/promises','tty','url','util','util/types','vm','zlib'];\n"
        "  __blue_node_core.module={ builtinModules:__builtinModules, createRequire:function(){ return require; }, isBuiltin:function(n){ n=String(n||'').replace(/^node:/,''); return __builtinModules.indexOf(n)>=0; } };\n"
        "  __blue_node_core.console=console;\n"
        "  __blue_node_core.process=process;\n"
        "  __blue_node_core.perf_hooks={ performance:{ now:function(){ return Date.now(); }, timeOrigin:Date.now() }, PerformanceObserver:function(){} };\n"
        "  __blue_node_core.punycode={ encode:function(s){return String(s||'');}, decode:function(s){return String(s||'');}, toASCII:function(s){return String(s||'');}, toUnicode:function(s){return String(s||'');} };\n"
        "  __blue_node_core.vm={ runInThisContext:function(code){ return (0,eval)(String(code)); }, runInNewContext:function(code,ctx){ ctx=ctx||{}; return Function('ctx','with(ctx){return eval('+JSON.stringify(String(code))+')}')(ctx); }, createContext:function(ctx){ return ctx||{}; }, isContext:function(){ return true; }, Script:function(code){ this.code=String(code); this.runInThisContext=function(){ return (0,eval)(this.code); }; this.runInNewContext=function(ctx){ return __blue_node_core.vm.runInNewContext(this.code,ctx); }; } };\n"
        "  __blue_node_core.child_process={ exec:function(_cmd,cb){ var e=new Error('child_process is not supported by Blue'); if(cb) setImmediate(function(){cb(e,'','');}); return { pid:0, kill:function(){}, on:function(){return this;} }; }, execSync:function(){ throw new Error('child_process is not supported by Blue'); }, spawn:function(){ throw new Error('child_process is not supported by Blue'); }, fork:function(){ throw new Error('child_process is not supported by Blue'); } };\n"
        "  __blue_node_core.dns={ lookup:function(host,opts,cb){ if(typeof opts==='function'){cb=opts;opts={};} setImmediate(function(){ if(cb) cb(null,String(host||'127.0.0.1'),4); }); }, promises:{ lookup:function(host){ return Promise.resolve({address:String(host||'127.0.0.1'),family:4}); } } };\n"
        "  __blue_node_core.readline={ createInterface:function(){ return { on:function(){return this;}, once:function(){return this;}, close:function(){}, question:function(_q,cb){ if(cb) cb(''); } }; } };\n"
        "  __blue_node_core.repl={ start:function(){ return { on:function(){return this;} }; } };\n"
        "  __blue_node_core.dgram={ createSocket:function(){ throw new Error('dgram is not supported by Blue'); } };\n"
        "  __blue_node_core.cluster={ isMaster:true, isPrimary:true, isWorker:false, workers:{} };\n"
        "  __blue_node_core.domain={ create:function(){ return new __EE(); } };\n"
        "  __blue_node_core.v8={ getHeapStatistics:function(){ return {}; }, serialize:function(v){ return Buffer.from(JSON.stringify(v)); }, deserialize:function(b){ return JSON.parse(Buffer.from(b).toString()); } };\n"
        "  __blue_node_core.worker_threads={ isMainThread:true, parentPort:null, Worker:function(){ throw new Error('worker_threads is not supported by Blue'); } };\n"
        "  __blue_node_core.diagnostics_channel={ channel:function(){ return { publish:function(){}, subscribe:function(){}, unsubscribe:function(){}, hasSubscribers:false }; }, hasSubscribers:function(){return false;} };\n"
        "  __blue_node_core['fs/promises']={ readFile:function(p,e){ return Promise.resolve(require('fs').readFileSync(p,e)); }, writeFile:function(p,d,o){ require('fs').writeFileSync(p,d,o); return Promise.resolve(); }, mkdir:function(p,o){ require('fs').mkdirSync(p,o); return Promise.resolve(); }, readdir:function(p){ return Promise.resolve(require('fs').readdirSync(p)); }, stat:function(p){ return Promise.resolve(require('fs').statSync(p)); }, unlink:function(p){ require('fs').unlinkSync(p); return Promise.resolve(); } };\n"
        "  try{ var __crypto=require('crypto'); __blue_node_core.crypto=__crypto; if(!globalThis.crypto) globalThis.crypto={}; if(!globalThis.crypto.getRandomValues) globalThis.crypto.getRandomValues=function(a){ var b=__crypto.randomBytes(a.length||0); for(var i=0;i<a.length;i++) a[i]=b[i]||0; return a; }; if(!globalThis.crypto.randomUUID) globalThis.crypto.randomUUID=function(){ var b=__crypto.randomBytes(16); b[6]=(b[6]&15)|64; b[8]=(b[8]&63)|128; var h=Buffer.from(b).toString('hex'); return h.slice(0,8)+'-'+h.slice(8,12)+'-'+h.slice(12,16)+'-'+h.slice(16,20)+'-'+h.slice(20); }; }catch(_e){}\n"
        "  globalThis.__blue_node_core=__blue_node_core;\n"
        "}\n";
    JSValue c = JS_Eval(ctx, coreCompatShim, std::strlen(coreCompatShim),
                        "<blue-core-compat>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(c)) {
        JSValue ex = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, ex);
        fprintf(stderr, "blue: core compat shim error: %s\n", msg ? msg : "?");
        if (msg)
            JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, ex);
    }
    JS_FreeValue(ctx, c);

    static const char* httpCompatShim =
        "if (!globalThis.__jsc_http_patch) {\n"
        "  function __JscEE(){ this.__ev = {}; }\n"
        "  __JscEE.prototype.on = function(n,f){ this.__ev=this.__ev||{}; (this.__ev[n]||(this.__ev[n]=[])).push(f); return this; };\n"
        "  __JscEE.prototype.addListener = __JscEE.prototype.on;\n"
        "  __JscEE.prototype.once = function(n,f){ var self=this; function w(){ self.removeListener(n,w); return f.apply(this, arguments);} return this.on(n,w); };\n"
        "  __JscEE.prototype.removeListener = function(n,f){ this.__ev=this.__ev||{}; var a=this.__ev[n]||[]; this.__ev[n]=a.filter(function(x){ return x!==f;}); return this; };\n"
        "  __JscEE.prototype.listeners = function(n){ this.__ev=this.__ev||{}; return (this.__ev[n]||[]).slice(); };\n"
        "  __JscEE.prototype.emit = function(n){ this.__ev=this.__ev||{}; var a=this.__ev[n]||[]; var args=[].slice.call(arguments,1); for(var i=0;i<a.length;i++) a[i].apply(this,args); return a.length>0; };\n"
        "  function __mkSocket(){ var s=new __JscEE(); s.readable=true; s.writable=true; s.destroyed=false; s.encrypted=false; s.setTimeout=function(){return s;}; s.destroy=function(){s.destroyed=true; s.emit('close');}; return s; }\n"
        "  function IncomingMessage(){ __JscEE.call(this); this.headers={}; this.rawHeaders=[]; this.trailers={}; this.httpVersion='1.1'; this.httpVersionMajor=1; this.httpVersionMinor=1; this.complete=true; this.aborted=false; this.readable=true; this.socket=__mkSocket(); this.connection=this.socket; }\n"
        "  IncomingMessage.prototype=Object.create(__JscEE.prototype); IncomingMessage.prototype.constructor=IncomingMessage;\n"
        "  IncomingMessage.prototype.pipe=function(dst){ this.on('data', function(c){ if(dst&&dst.write) dst.write(c); }); this.on('end', function(){ if(dst&&dst.end) dst.end(); }); return dst||this; };\n"
        "  IncomingMessage.prototype.unpipe=function(){ return this; };\n"
        "  IncomingMessage.prototype.pause=function(){ return this; };\n"
        "  IncomingMessage.prototype.resume=function(){ this.emit('resume'); this.emit('end'); return this; };\n"
        "  IncomingMessage.prototype.setTimeout=function(){ return this; };\n"
        "  function ServerResponse(){ __JscEE.call(this); this.statusCode=200; this.statusMessage='OK'; this.headersSent=false; this.finished=false; this.writableEnded=false; this._headers={}; this._body=''; this.socket=null; this.connection=null; }\n"
        "  ServerResponse.prototype=Object.create(__JscEE.prototype); ServerResponse.prototype.constructor=ServerResponse;\n"
        "  ServerResponse.prototype.setHeader=function(k,v){ this._headers[String(k).toLowerCase()] = String(v); return this; };\n"
        "  ServerResponse.prototype.getHeader=function(k){ return this._headers[String(k).toLowerCase()]; };\n"
        "  ServerResponse.prototype.removeHeader=function(k){ delete this._headers[String(k).toLowerCase()]; return this; };\n"
        "  ServerResponse.prototype.getHeaders=function(){ return this._headers; };\n"
        "  ServerResponse.prototype.writeHead=function(code,reason,hdrs){ this.statusCode=(code|0)||200; if(typeof reason==='object'&&reason) hdrs=reason; if(hdrs&&typeof hdrs==='object'){ for(var k in hdrs) this.setHeader(k,hdrs[k]); } this.headersSent=true; return this; };\n"
        "  ServerResponse.prototype.write=function(chunk){ this.headersSent=true; if(chunk!=null){ this._bodyChunks=this._bodyChunks||[]; this._bodyChunks.push(chunk); if(chunk&&typeof chunk==='object'&&typeof chunk.byteLength==='number') this._hasBinary=true; } this.emit('drain'); return true; };\n"
        "  ServerResponse.prototype.end=function(chunk){ if(chunk!=null) this.write(chunk); this.headersSent=true; this.finished=true; this.writableEnded=true; var chunks=this._bodyChunks||[]; if(this._hasBinary){ var t=0; for(var i=0;i<chunks.length;i++){ var c=chunks[i]; t+=(c&&typeof c==='object'&&typeof c.byteLength==='number')?c.byteLength:String(c).length; } var out=new Uint8Array(t),off=0; for(var i=0;i<chunks.length;i++){ var c=chunks[i]; if(c&&typeof c==='object'&&typeof c.byteLength==='number'){ out.set(new Uint8Array(c.buffer||c,c.byteOffset||0,c.byteLength),off); off+=c.byteLength; }else{ var s=String(c); for(var j=0;j<s.length;j++) out[off++]=s.charCodeAt(j)&0xff; } } if(typeof this.__nativeBinaryEnd==='function') this.__nativeBinaryEnd(out,this.statusCode,this._headers); } else { var body=''; for(var i=0;i<chunks.length;i++) body+=String(chunks[i]); if(typeof this.__nativeEnd==='function') this.__nativeEnd(body,this.statusCode,this._headers); } this.emit('finish'); if(this.socket&&this.socket.emit) this.socket.emit('close'); this.emit('close'); return this; };\n"
        "  ServerResponse.prototype.status=function(code){ this.statusCode=code|0; return this; };\n"
        "  ServerResponse.prototype.send=function(body){ if(body&&typeof body==='object'&&!(body instanceof Uint8Array)){ this.setHeader('content-type','application/json; charset=utf-8'); body=JSON.stringify(body); } this.end(body==null?'':body); return this; };\n"
        "  ServerResponse.prototype.json=function(obj){ this.setHeader('content-type','application/json; charset=utf-8'); this.end(JSON.stringify(obj==null?{}:obj)); return this; };\n"
        "  ServerResponse.prototype.setTimeout=function(){ return this; };\n"
        "  globalThis.__jsc_http_IncomingMessage = IncomingMessage;\n"
        "  globalThis.__jsc_http_ServerResponse = ServerResponse;\n"
        "  function __parseHeaders(text){\n"
        "    var headers={}; var raw=[];\n"
        "    if(!text) return {headers:headers,raw:raw};\n"
        "    var lines=String(text).split('\\r\\n');\n"
        "    for(var i=0;i<lines.length;i++){\n"
        "      var ln=lines[i]; if(!ln) continue;\n"
        "      var p=ln.indexOf(':'); if(p<=0) continue;\n"
        "      var k=ln.slice(0,p).trim().toLowerCase();\n"
        "      var v=ln.slice(p+1).trim();\n"
        "      if(!k) continue;\n"
        "      raw.push(k, v);\n"
        "      if(headers[k]===undefined) headers[k]=v;\n"
        "      else if(Array.isArray(headers[k])) headers[k].push(v);\n"
        "      else headers[k]=[headers[k], v];\n"
        "    }\n"
        "    return {headers:headers,raw:raw};\n"
        "  }\n"
        "  function __normalizeUrl(u){\n"
        "    var s=String(u||'/');\n"
        "    if(s.indexOf('http://')===0||s.indexOf('https://')===0){\n"
        "      var slash=s.indexOf('/', s.indexOf('://')+3);\n"
        "      s = slash>=0 ? s.slice(slash) : '/';\n"
        "    }\n"
        "    return s || '/';\n"
        "  }\n"
        "  globalThis.__jsc_http_patch = function(req,res,method,url,body,headersText){\n"
        "    Object.setPrototypeOf(req, IncomingMessage.prototype); IncomingMessage.call(req);\n"
        "    var parsed=__parseHeaders(headersText);\n"
        "    req.method = String(method || 'GET').toUpperCase(); req.url = __normalizeUrl(url); req.originalUrl=req.url; req.baseUrl='';\n"
        "    req.headers = parsed.headers; req.rawHeaders = parsed.raw;\n"
        "    req._parsedUrl = undefined;\n"
        "    req._parsedOriginalUrl = undefined;\n"
        "    req.rawBody = body || ''; req.body = undefined;\n"
        "    req.readableEnded = false;\n"
        "    req.complete = false;\n"
        "    var __ended = false;\n"
        "    req.resume = function(){\n"
        "      if (__ended) return req;\n"
        "      __ended = true;\n"
        "      if (req.rawBody && req.rawBody.length) req.emit('data', req.rawBody);\n"
        "      req.readable = false;\n"
        "      req.readableEnded = true;\n"
        "      req.complete = true;\n"
        "      req.emit('end');\n"
        "      return req;\n"
        "    };\n"
        "    Object.setPrototypeOf(res, ServerResponse.prototype); ServerResponse.call(res);\n"
        "    res.req=req; res.socket=req.socket; res.connection=req.connection; res.locals={};\n"
        "    setImmediate(function(){ req.resume(); });\n"
        "    return [req,res];\n"
        "  };\n"
        "}\n";
    JSValue h = JS_Eval(ctx, httpCompatShim, std::strlen(httpCompatShim),
                        "<blue-http-compat>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(h)) {
        JSValue ex = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, ex);
        fprintf(stderr, "blue: http compat shim error: %s\n", msg ? msg : "?");
        if (msg)
            JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, ex);
    }
    JS_FreeValue(ctx, h);

    jsc_qjs_install_blue_desktop(ctx);
}

#ifdef JSC_QJS_CONTROL_PLANE
extern "C" void jsc_control_plane_run_embedded(const unsigned char* bundle,
                                               size_t len) {
    auto tCtl0 = std::chrono::steady_clock::now();
    jsc_quickjs_ensure_runtime();
    JSContext* ctx = jsc_quickjs_get_context();
    if (!ctx || !bundle || len == 0) {
        fprintf(stderr, "blue: control plane: missing context or bundle\n");
        return;
    }

    jsc_qjs_install_control_plane_shims(ctx);

    JSValue ret = JS_Eval(ctx, (const char*)bundle, len, "blue-npm-bundle.js",
                          JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ret)) {
        JSValue ex = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, ex);
        fprintf(stderr, "blue: bundle eval error: %s\n", msg ? msg : "?");
        if (msg)
            JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, ex);
    }
    JS_FreeValue(ctx, ret);
    jsc_quickjs_pump_microtasks();
    auto tCtl1          = std::chrono::steady_clock::now();
    long long elapsedMs = (long long)std::chrono::duration_cast<
        std::chrono::milliseconds>(tCtl1 - tCtl0)
                              .count();
    blue_write_boot_metric_ms(elapsedMs);
}
#endif /* JSC_QJS_CONTROL_PLANE */

#elif defined(JSC_QJS_CONTROL_PLANE) || defined(JSC_HYBRID)

extern "C" void jsc_qjs_free_http_opaque(void* opaque) {
    if (opaque) {
        auto* qh = (QjsHttpHandler*)opaque;
        if (qh->ctx)
            JS_FreeValue(qh->ctx, qh->fn);
        std::free(qh);
    }
}

#ifdef JSC_QJS_CONTROL_PLANE
extern "C" void jsc_control_plane_run_embedded(const unsigned char* bundle,
                                               size_t len) {
    (void)bundle;
    (void)len;
    fprintf(stderr,
            "blue: control plane requires JSC_RUNTIME_NODE_IO and "
            "JSC_HAVE_UV\n");
}
#endif

#endif
