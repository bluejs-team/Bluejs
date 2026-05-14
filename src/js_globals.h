/*
 * js_globals.h - Node boot shims for blue binaries: unified globals, Buffer MVP,
 * and smart member-call dispatch (e.g. Buffer#toString on JS_BYTES).
 */

#ifndef JS_GLOBALS_H
#define JS_GLOBALS_H

#include "js_value.h"
#include "runtime/blue_plugin.h"
#include "runtime/jsc_blue_native.h"
#include "runtime/jsc_runtime.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

/* ── Base64 helpers (minimal, for Buffer.from(..., 'base64')) ─────────────── */

static inline unsigned char js_b64_value(char c) {
    static const signed char tbl[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1 };
    unsigned char uc = (unsigned char)c;
    return (unsigned char)(tbl[uc] >= 0 ? tbl[uc] : 255);
}

/* Decodes ASCII base64 (ignores whitespace). Returns malloc'd buffer + len out. */
static inline unsigned char* js_buffer_decode_base64_alloc(const char* in, size_t* out_len,
                                                          int* err) {
    *err = 0;
    *out_len = 0;
    size_t nin = strlen(in ? in : "");
    char* clean = (char*)malloc(nin + 1);
    if (!clean) { *err = 1; return NULL; }
    size_t j = 0;
    for (size_t i = 0; i < nin; i++) {
        char c = in[i];
        if (isspace((unsigned char)c)) continue;
        clean[j++] = c;
    }
    clean[j] = '\0';
    if (j % 4 != 0) {
        free(clean); *err = 1;
        return NULL;
    }
    size_t qlen = j;
    size_t nbytes = qlen ? (qlen / 4) * 3 : 0;
    if (qlen >= 1 && clean[qlen - 1] == '=') nbytes--;
    if (qlen >= 2 && clean[qlen - 2] == '=') nbytes--;
    unsigned char* out = (unsigned char*)malloc(nbytes ? nbytes : 1);
    if (!out) { free(clean); *err = 1; return NULL; }
    size_t o = 0;
    for (size_t q = 0; q + 4 <= qlen; q += 4) {
        unsigned char a = js_b64_value(clean[q]);
        unsigned char b = js_b64_value(clean[q + 1]);
        unsigned char c = js_b64_value(clean[q + 2]);
        unsigned char d = js_b64_value(clean[q + 3]);
        if (a > 63 || b > 63 || c > 254 || d > 254) {
            free(clean); free(out); *err = 1;
            return NULL;
        }
        unsigned int triple = ((unsigned)a << 18) | ((unsigned)b << 12) |
                              ((clean[q + 2] == '=' ? 0 : c) << 6) |
                              (clean[q + 3] == '=' ? 0 : d);
        if (o < nbytes) out[o++] = (unsigned char)((triple >> 16) & 0xff);
        if (clean[q + 2] != '=' && o < nbytes)
            out[o++] = (unsigned char)((triple >> 8) & 0xff);
        if (clean[q + 3] != '=' && o < nbytes)
            out[o++] = (unsigned char)(triple & 0xff);
    }
    free(clean);
    *out_len = nbytes;
    return out;
}

/* Encode JS_BYTES → base64 C string (malloc’d). Caller frees. */
static inline char* js_buffer_encode_base64_alloc(JsBytes* b, int* err) {
    *err = 0;
    if (!b) { *err = 1; return NULL; }
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t lin = b->len;
    size_t out_chars = lin ? (((lin + 2) / 3) * 4) + 1 : 1;
    char* op = (char*)malloc(out_chars);
    if (!op) { *err = 1; return NULL; }
    size_t o = 0;
    for (size_t i = 0; i < lin; i += 3) {
        uint32_t triple = ((uint32_t)b->data[i]) << 16;
        if (i + 1 < lin) triple |= ((uint32_t)b->data[i + 1]) << 8;
        if (i + 2 < lin) triple |= (uint32_t)b->data[i + 2];
        op[o++] = tbl[(triple >> 18) & 63];
        op[o++] = tbl[(triple >> 12) & 63];
        op[o++] = i + 1 < lin ? tbl[(triple >> 6) & 63] : '=';
        op[o++] = i + 2 < lin ? tbl[triple & 63] : '=';
    }
    op[o] = '\0';
    return op;
}

static inline int js_buffer_enc_is_utf8(JsValue enc) {
    char* es = js_to_cstr(enc);
    int utf8 =
        strcmp(es ? es : "", "utf8") == 0 ||
        strcmp(es ? es : "", "utf-8") == 0 ||
        enc.tag == JS_UNDEFINED;
    free(es);
    return utf8;
}

static inline int js_buffer_enc_is_base64(JsValue enc) {
    char* es = js_to_cstr(enc);
    int b =
        strcmp(es ? es : "", "base64") == 0 || strcmp(es ? es : "", "BASE64") == 0;
    free(es);
    return b;
}

static inline JsValue js_buffer_byte_length_prop(JsValue v) {
    if (v.tag != JS_BYTES || !v.as.bytes) return JS_NUM(0);
    return JS_NUM((double)v.as.bytes->len);
}

/*
 * Buffer-ish .toString / Buffer.prototype.read semantics for JS_BYTES tags.
 */
static inline JsValue js_buffer_toString(JsValue v, JsValue enc) {
    if (v.tag != JS_BYTES || !v.as.bytes) return JS_UNDEFINED();
    if (js_buffer_enc_is_base64(enc)) {
        int e = 0;
        char* s = js_buffer_encode_base64_alloc(v.as.bytes, &e);
        if (e || !s) return JS_UNDEFINED();
        JsValue r = JS_STR(s);
        free(s);
        return r;
    }
    if (js_buffer_enc_is_utf8(enc)) {
        JsBytes* b = v.as.bytes;
        unsigned char* cpy = (unsigned char*)malloc(b->len + 1);
        if (!cpy) return JS_UNDEFINED();
        if (b->len) memcpy(cpy, b->data, b->len);
        cpy[b->len] = '\0';
        JsValue rv;
        rv.tag = JS_STRING;
        rv.as.string = (char*)cpy;
        return rv;
    }
    return JS_UNDEFINED();
}

static inline JsValue js_buffer_slice(JsValue v, JsValue startVal,
                                      JsValue endVal) {
    if (v.tag != JS_BYTES || !v.as.bytes || !v.as.bytes->data)
        return (JsValue){JS_UNDEFINED, {0}};
    size_t    len = v.as.bytes->len;
    long long st  = (long long)js_to_num(startVal);
    long long en;
    if (endVal.tag == JS_UNDEFINED)
        en = (long long)len;
    else
        en = (long long)js_to_num(endVal);
    if (st < 0)
        st += (long long)len;
    if (en < 0)
        en += (long long)len;
    if (st < 0)
        st = 0;
    if ((size_t)st > len)
        st = (long long)len;
    if (en < st)
        en = st;
    if ((size_t)en > len)
        en = (long long)len;
    size_t           n = (size_t)(en - st);
    unsigned char* p = (unsigned char*)malloc(n ? n : 1);
    if (!p)
        return (JsValue){JS_UNDEFINED, {0}};
    if (n)
        memcpy(p, v.as.bytes->data + (size_t)st, n);
    return js_byte_buffer_own(p, n);
}

static inline JsValue js_buffer_readUIntBE(JsValue v, JsValue offsetVal,
                                           JsValue byteLenVal) {
    if (v.tag != JS_BYTES || !v.as.bytes || !v.as.bytes->data)
        return (JsValue){JS_UNDEFINED, {0}};
    size_t off = (size_t)js_to_num(offsetVal);
    int    bl  = (int)js_to_num(byteLenVal);
    if (bl < 1 || bl > 6 || off + (size_t)bl > v.as.bytes->len)
        return (JsValue){JS_UNDEFINED, {0}};
    uint64_t x = 0;
    for (int i = 0; i < bl; ++i)
        x = (x << 8) | (uint64_t)v.as.bytes->data[off + (size_t)i];
    return JS_NUM((double)x);
}

static inline JsValue js_buffer_compare(JsValue a, JsValue b) {
    if (a.tag != JS_BYTES || !a.as.bytes || !a.as.bytes->data ||
        b.tag != JS_BYTES || !b.as.bytes || !b.as.bytes->data)
        return (JsValue){JS_UNDEFINED, {0}};
    size_t na = a.as.bytes->len;
    size_t nb = b.as.bytes->len;
    size_t n  = na < nb ? na : nb;
    int    cmp =
        memcmp(a.as.bytes->data, b.as.bytes->data, n);
    if (cmp != 0)
        return JS_NUM(cmp < 0 ? -1.0 : 1.0);
    if (na < nb)
        return JS_NUM(-1.0);
    if (na > nb)
        return JS_NUM(1.0);
    return JS_NUM(0.0);
}

static inline JsValue js_buffer_copy(JsValue src, JsValue tgt, JsValue targetStart,
                                     JsValue sourceStart, JsValue sourceEnd) {
    if (src.tag != JS_BYTES || !src.as.bytes || !src.as.bytes->data ||
        tgt.tag != JS_BYTES || !tgt.as.bytes || !tgt.as.bytes->data)
        return (JsValue){JS_UNDEFINED, {0}};
    size_t slen = src.as.bytes->len;
    size_t tlen = tgt.as.bytes->len;
    size_t t0   = (size_t)js_to_num(targetStart);
    long long s0ll = (long long)js_to_num(sourceStart);
    long long s1ll;
    if (sourceEnd.tag == JS_UNDEFINED)
        s1ll = (long long)slen;
    else
        s1ll = (long long)js_to_num(sourceEnd);
    if (s0ll < 0)
        s0ll = 0;
    if ((size_t)s0ll > slen)
        s0ll = (long long)slen;
    if (s1ll < s0ll)
        s1ll = s0ll;
    if ((size_t)s1ll > slen)
        s1ll = (long long)slen;
    size_t n = (size_t)(s1ll - s0ll);
    if (t0 > tlen)
        t0 = tlen;
    if (n > tlen - t0)
        n = tlen - t0;
    if (n == 0)
        return JS_NUM(0.0);
    memmove(tgt.as.bytes->data + t0, src.as.bytes->data + (size_t)s0ll, n);
    return JS_NUM((double)n);
}

static inline JsValue js_buffer_concat(JsValue a, JsValue b) {
    size_t na = (a.tag == JS_BYTES && a.as.bytes && a.as.bytes->data)
                    ? a.as.bytes->len
                    : 0;
    size_t nb = (b.tag == JS_BYTES && b.as.bytes && b.as.bytes->data)
                    ? b.as.bytes->len
                    : 0;
    size_t nt = na + nb;
    unsigned char* nd = (unsigned char*)malloc(nt ? nt : 1);
    if (!nd) return (JsValue){JS_UNDEFINED, {0}};
    if (na) memcpy(nd, a.as.bytes->data, na);
    if (nb) memcpy(nd + na, b.as.bytes->data, nb);
    return js_byte_buffer_own(nd, nt);
}

static inline JsValue js_buffer_alloc(JsValue sizeVal, JsValue fillVal) {
    size_t n = (size_t)js_to_num(sizeVal);
    if (isnan(js_to_num(sizeVal)) || js_to_num(sizeVal) < 0 || n > 16777216u)
        n = 0;
    unsigned char fill = '\0';
    if (fillVal.tag == JS_STRING && fillVal.as.string &&
        fillVal.as.string[0])
        fill = (unsigned char)fillVal.as.string[0];
    unsigned char* p = (unsigned char*)calloc(n ? n : 1, n ? 1 : 1);
    if (!p) return (JsValue){JS_UNDEFINED, {0}};
    if (n && fillVal.tag != JS_UNDEFINED)
        memset(p, fill, n);
    return js_byte_buffer_own(p, n);
}

static inline JsValue js_buffer_from(JsValue input, JsValue enc) {
    if (input.tag == JS_NUMBER) return js_buffer_alloc(input, JS_UNDEFINED());
    if (input.tag != JS_STRING || !input.as.string)
        return (JsValue){JS_UNDEFINED, {0}};
    if (js_buffer_enc_is_base64(enc)) {
        size_t bn = 0;
        int e = 0;
        unsigned char* raw =
            js_buffer_decode_base64_alloc(input.as.string, &bn, &e);
        if (e) return (JsValue){JS_UNDEFINED, {0}};
        return js_byte_buffer_own(
            raw ? raw : (unsigned char*)calloc(1, 1), bn);
    }
    /* utf8/default: treat string bytes verbatim */
    size_t sl = strlen(input.as.string);
    unsigned char* p = (unsigned char*)malloc(sl + 1);
    if (!p) return (JsValue){JS_UNDEFINED, {0}};
    memcpy(p, input.as.string, sl);
    p[sl] = '\0';
    return js_byte_buffer_own(p, sl);
}

static inline JsValue _js_buf_from_w(int argc, JsValue* argv) {
    JsValue inp = argc > 0 ? argv[0] : JS_UNDEFINED();
    JsValue enc = argc > 1 ? argv[1] : JS_UNDEFINED();
    return js_buffer_from(inp, enc);
}

static inline JsValue _js_buf_alloc_w(int argc, JsValue* argv) {
    return js_buffer_alloc(argc > 0 ? argv[0] : JS_UNDEFINED(),
                           argc > 1 ? argv[1] : JS_UNDEFINED());
}

static inline JsValue _js_buf_concat_w(int argc, JsValue* argv) {
    JsValue a = argc > 0 ? argv[0] : JS_UNDEFINED();
    JsValue b = argc > 1 ? argv[1] : JS_UNDEFINED();
    return js_buffer_concat(a, b);
}

static inline JsValue _js_buf_isBuffer_w(int argc, JsValue* argv) {
    return JS_BOOL(argc > 0 && argv[0].tag == JS_BYTES);
}

/* Built-in singleton for globalThis.Buffer / require('buffer') root. */
static inline JsValue js_builtin_buffer_namespace(void) {
    JsValue m = js_obj_new();
    js_obj_set_fn(&m, "from", _js_buf_from_w);
    js_obj_set_fn(&m, "alloc", _js_buf_alloc_w);
    js_obj_set_fn(&m, "concat", _js_buf_concat_w);
    js_obj_set_fn(&m, "isBuffer", _js_buf_isBuffer_w);
    return m;
}

/* node:buffer shim - `{ Buffer }` bundle */
static inline JsValue js_node_buffer_module_bundle(void) {
    JsValue b = js_builtin_buffer_namespace();
    JsValue m = js_obj_new();
    js_obj_set(&m, "Buffer", b);
    return m;
}

/* ── Extra console hooks ──────────────────────────────────────────────────── */

static inline JsValue _builtin_console_log_w(int argc, JsValue* argv) {
    for (int i = 0; i < argc; i++) {
        if (i) fputc(' ', stdout);
        char* ts = js_to_cstr(argv[i]);
        fputs(ts ? ts : "", stdout);
        free(ts);
    }
    fputc('\n', stdout);
    return JS_UNDEFINED();
}

static inline JsValue _builtin_console_err_w(int argc, JsValue* argv) {
    for (int i = 0; i < argc; i++) {
        if (i) fputc(' ', stderr);
        char* ts = js_to_cstr(argv[i]);
        fputs(ts ? ts : "", stderr);
        free(ts);
    }
    fputc('\n', stderr);
    return JS_UNDEFINED();
}

static inline JsValue _js_console_warn_w(int argc, JsValue* argv) {
    (void)fprintf(stderr, "[warn] ");
    for (int i = 0; i < argc; i++) {
        if (i) fputc(' ', stderr);
        char* ts = js_to_cstr(argc > i ? argv[i] : JS_UNDEFINED());
        fputs(ts ? ts : "", stderr);
        free(ts);
    }
    fputc('\n', stderr);
    return JS_UNDEFINED();
}

static inline JsValue _js_console_info_w(int argc, JsValue* argv) {
    (void)fprintf(stdout, "[info] ");
    for (int i = 0; i < argc; i++) {
        if (i) fputc(' ', stdout);
        char* ts = js_to_cstr(argv[i]);
        fputs(ts ? ts : "", stdout);
        free(ts);
    }
    fputc('\n', stdout);
    return JS_UNDEFINED();
}

static inline JsValue _js_console_debug_w(int argc, JsValue* argv) {
    return _js_console_info_w(argc, argv); /* simplified */
}

static inline JsValue _js_console_trace_w(int argc, JsValue* argv) {
    return _js_console_warn_w(argc, argv);
}

static inline JsValue _js_console_assert_w(int argc, JsValue* argv) {
    if (argc <= 0) return JS_UNDEFINED();
    if (js_truthy(argv[0])) return JS_UNDEFINED();
    fprintf(stderr, "Assertion failed");
    if (argc > 1) {
        fputc(' ', stderr);
        for (int i = 1; i < argc; i++) {
            if (i > 1) fputc(' ', stderr);
            char* ts = js_to_cstr(argv[i]);
            fputs(ts ? ts : "", stderr);
            free(ts);
        }
    }
    fputc('\n', stderr);
    return JS_UNDEFINED();
}

static inline JsValue _builtin_math1(int argc, JsValue* argv, double (*fn)(double)) {
    double x = (argc > 0) ? js_to_num(argv[0]) : 0.0;
    return js_num(fn(x));
}

static inline JsValue _builtin_math_abs_w(int argc, JsValue* argv) {
    return _builtin_math1(argc, argv, fabs);
}
static inline JsValue _builtin_math_sqrt_w(int argc, JsValue* argv) {
    return _builtin_math1(argc, argv, sqrt);
}
static inline JsValue _builtin_math_sin_w(int argc, JsValue* argv) {
    return _builtin_math1(argc, argv, sin);
}
static inline JsValue _builtin_math_cos_w(int argc, JsValue* argv) {
    return _builtin_math1(argc, argv, cos);
}
static inline JsValue _builtin_math_floor_w(int argc, JsValue* argv) {
    return _builtin_math1(argc, argv, floor);
}
static inline JsValue _builtin_math_ceil_w(int argc, JsValue* argv) {
    return _builtin_math1(argc, argv, ceil);
}
static inline JsValue _builtin_math_round_w(int argc, JsValue* argv) {
    return _builtin_math1(argc, argv, round);
}
static inline JsValue _builtin_math_pow_w(int argc, JsValue* argv) {
    double x = (argc > 0) ? js_to_num(argv[0]) : 0.0;
    double y = (argc > 1) ? js_to_num(argv[1]) : 0.0;
    return js_num(pow(x, y));
}
static inline JsValue _builtin_math_max_w(int argc, JsValue* argv) {
    if (argc <= 0) return JS_NUM(-INFINITY);
    double m = js_to_num(argv[0]);
    for (int i = 1; i < argc; ++i) {
        double v = js_to_num(argv[i]);
        if (v > m) m = v;
    }
    return js_num(m);
}
static inline JsValue _builtin_math_min_w(int argc, JsValue* argv) {
    if (argc <= 0) return JS_NUM(INFINITY);
    double m = js_to_num(argv[0]);
    for (int i = 1; i < argc; ++i) {
        double v = js_to_num(argv[i]);
        if (v < m) m = v;
    }
    return js_num(m);
}

static inline JsValue js_builtin_console(void) {
    JsValue co = js_obj_new();
    js_obj_set_fn(&co, "log",   _builtin_console_log_w);
    js_obj_set_fn(&co, "error", _builtin_console_err_w);
    js_obj_set_fn(&co, "warn",  _js_console_warn_w);
    js_obj_set_fn(&co, "info",  _js_console_info_w);
    js_obj_set_fn(&co, "debug", _js_console_debug_w);
    js_obj_set_fn(&co, "trace", _js_console_trace_w);
    js_obj_set_fn(&co, "assert", _js_console_assert_w);
    return co;
}

static inline JsValue js_builtin_math(void) {
    JsValue m = js_obj_new();
    js_obj_set_fn(&m, "abs", _builtin_math_abs_w);
    js_obj_set_fn(&m, "sqrt", _builtin_math_sqrt_w);
    js_obj_set_fn(&m, "sin", _builtin_math_sin_w);
    js_obj_set_fn(&m, "cos", _builtin_math_cos_w);
    js_obj_set_fn(&m, "floor", _builtin_math_floor_w);
    js_obj_set_fn(&m, "ceil", _builtin_math_ceil_w);
    js_obj_set_fn(&m, "round", _builtin_math_round_w);
    js_obj_set_fn(&m, "pow", _builtin_math_pow_w);
    js_obj_set_fn(&m, "max", _builtin_math_max_w);
    js_obj_set_fn(&m, "min", _builtin_math_min_w);
    return m;
}

/*
 * Dispatches `(obj.prop)(...)`: supports JS_BYTES#toString(encoding) etc.
 */
static inline JsValue js_jsc_call_member(JsValue recv, JsValue methName, int argc,
                                         JsValue* argv) {
    char* key = js_to_cstr(methName);
    if (!key) key = js_strdup("");

    {
        int io_handled = 0;
        JsValue io_r =
            jsc_io_try_member_call(recv, methName, argc, argv, &io_handled);
        if (io_handled) {
            free(key);
            return io_r;
        }
    }

    /* ── String instance methods ────────────────────────────────────────────── */
    if (recv.tag == JS_STRING) {
        const char* s = recv.as.string ? recv.as.string : "";
        size_t slen = strlen(s);

        if (strcmp(key, "toUpperCase") == 0 || strcmp(key, "toLocaleUpperCase") == 0) {
            char* r = js_strdup(s);
            for (size_t i = 0; i < slen; ++i) r[i] = (char)toupper((unsigned char)r[i]);
            JsValue v = js_str(r); free(r); free(key); return v;
        }
        if (strcmp(key, "toLowerCase") == 0 || strcmp(key, "toLocaleLowerCase") == 0) {
            char* r = js_strdup(s);
            for (size_t i = 0; i < slen; ++i) r[i] = (char)tolower((unsigned char)r[i]);
            JsValue v = js_str(r); free(r); free(key); return v;
        }
        if (strcmp(key, "trim") == 0) {
            const char* start = s;
            while (*start && isspace((unsigned char)*start)) start++;
            const char* end = s + slen;
            while (end > start && isspace((unsigned char)*(end-1))) end--;
            size_t n = (size_t)(end - start);
            char* r = (char*)malloc(n + 1); memcpy(r, start, n); r[n] = '\0';
            JsValue v = js_str(r); free(r); free(key); return v;
        }
        if (strcmp(key, "trimStart") == 0 || strcmp(key, "trimLeft") == 0) {
            const char* start = s;
            while (*start && isspace((unsigned char)*start)) start++;
            JsValue v = js_str(start); free(key); return v;
        }
        if (strcmp(key, "trimEnd") == 0 || strcmp(key, "trimRight") == 0) {
            char* r = js_strdup(s);
            size_t n = strlen(r);
            while (n > 0 && isspace((unsigned char)r[n-1])) r[--n] = '\0';
            JsValue v = js_str(r); free(r); free(key); return v;
        }
        if (strcmp(key, "includes") == 0) {
            const char* needle = argc > 0 ? (argv[0].as.string ? argv[0].as.string : "") : "";
            JsValue v = js_bool(strstr(s, needle) != NULL); free(key); return v;
        }
        if (strcmp(key, "startsWith") == 0) {
            const char* needle = argc > 0 ? (argv[0].as.string ? argv[0].as.string : "") : "";
            size_t nlen = strlen(needle);
            JsValue v = js_bool(strncmp(s, needle, nlen) == 0); free(key); return v;
        }
        if (strcmp(key, "endsWith") == 0) {
            const char* needle = argc > 0 ? (argv[0].as.string ? argv[0].as.string : "") : "";
            size_t nlen = strlen(needle);
            JsValue v = js_bool(slen >= nlen && strcmp(s + slen - nlen, needle) == 0);
            free(key); return v;
        }
        if (strcmp(key, "indexOf") == 0) {
            const char* needle = argc > 0 ? (argv[0].as.string ? argv[0].as.string : "") : "";
            const char* found = strstr(s, needle);
            JsValue v = js_num(found ? (double)(found - s) : -1.0); free(key); return v;
        }
        if (strcmp(key, "lastIndexOf") == 0) {
            const char* needle = argc > 0 ? (argv[0].as.string ? argv[0].as.string : "") : "";
            size_t nlen = strlen(needle);
            const char* last = NULL;
            if (nlen == 0) { last = s + slen; }
            else { for (size_t i = 0; i + nlen <= slen; ++i) if (memcmp(s+i, needle, nlen) == 0) last = s+i; }
            JsValue v = js_num(last ? (double)(last - s) : -1.0); free(key); return v;
        }
        if (strcmp(key, "slice") == 0 || strcmp(key, "substring") == 0) {
            long start_i = argc > 0 ? (long)js_to_num(argv[0]) : 0;
            long end_i   = argc > 1 ? (long)js_to_num(argv[1]) : (long)slen;
            if (strcmp(key, "substring") == 0) {
                if (start_i < 0) start_i = 0;
                if (end_i   < 0) end_i   = 0;
                if (start_i > end_i) { long t = start_i; start_i = end_i; end_i = t; }
            } else {
                if (start_i < 0) start_i = (long)slen + start_i;
                if (end_i   < 0) end_i   = (long)slen + end_i;
            }
            if (start_i < 0) start_i = 0;
            if (end_i   < 0) end_i   = 0;
            if (start_i > (long)slen) start_i = (long)slen;
            if (end_i   > (long)slen) end_i   = (long)slen;
            size_t n = start_i <= end_i ? (size_t)(end_i - start_i) : 0;
            char* r = (char*)malloc(n + 1); memcpy(r, s + start_i, n); r[n] = '\0';
            JsValue v = js_str(r); free(r); free(key); return v;
        }
        if (strcmp(key, "at") == 0) {
            long idx = argc > 0 ? (long)js_to_num(argv[0]) : 0;
            if (idx < 0) idx = (long)slen + idx;
            if (idx < 0 || idx >= (long)slen) { free(key); return JS_UNDEFINED(); }
            char buf[2] = { s[idx], '\0' };
            JsValue v = js_str(buf); free(key); return v;
        }
        if (strcmp(key, "charAt") == 0) {
            long idx = argc > 0 ? (long)js_to_num(argv[0]) : 0;
            if (idx < 0 || idx >= (long)slen) { free(key); return js_str(""); }
            char buf[2] = { s[idx], '\0' };
            JsValue v = js_str(buf); free(key); return v;
        }
        if (strcmp(key, "charCodeAt") == 0) {
            long idx = argc > 0 ? (long)js_to_num(argv[0]) : 0;
            if (idx < 0 || idx >= (long)slen) { free(key); return js_num(NAN); }
            JsValue v = js_num((double)(unsigned char)s[idx]); free(key); return v;
        }
        if (strcmp(key, "repeat") == 0) {
            int n = argc > 0 ? (int)js_to_num(argv[0]) : 0;
            if (n <= 0) { free(key); return js_str(""); }
            char* r = (char*)malloc(slen * (size_t)n + 1); r[0] = '\0';
            for (int i = 0; i < n; ++i) memcpy(r + slen*(size_t)i, s, slen);
            r[slen*(size_t)n] = '\0';
            JsValue v = js_str(r); free(r); free(key); return v;
        }
        if (strcmp(key, "padStart") == 0) {
            double td = argc > 0 ? js_to_num(argv[0]) : 0;
            if (td <= (double)slen || td < 1) { free(key); return js_clone_value(recv); }
            size_t target = (size_t)td;
            const char* fill = argc > 1 && argv[1].as.string ? argv[1].as.string : " ";
            size_t flen = strlen(fill);
            if (flen == 0) { free(key); return js_clone_value(recv); }
            size_t pad = target - slen;
            char* r = (char*)malloc(target + 2);
            for (size_t i = 0; i < pad; ++i) r[i] = fill[i % flen];
            memcpy(r + pad, s, slen); r[target] = '\0';
            JsValue v = js_str(r); free(r); free(key); return v;
        }
        if (strcmp(key, "padEnd") == 0) {
            double td = argc > 0 ? js_to_num(argv[0]) : 0;
            if (td <= (double)slen || td < 1) { free(key); return js_clone_value(recv); }
            size_t target = (size_t)td;
            const char* fill = argc > 1 && argv[1].as.string ? argv[1].as.string : " ";
            size_t flen = strlen(fill);
            if (flen == 0) { free(key); return js_clone_value(recv); }
            char* r = (char*)malloc(target + 2);
            memcpy(r, s, slen);
            for (size_t i = slen; i < target; ++i) r[i] = fill[(i - slen) % flen];
            r[target] = '\0';
            JsValue v = js_str(r); free(r); free(key); return v;
        }
        if (strcmp(key, "replace") == 0 || strcmp(key, "replaceAll") == 0) {
            int all = strcmp(key, "replaceAll") == 0;
            const char* search = argc > 0 && argv[0].as.string ? argv[0].as.string : "";
            const char* repl   = argc > 1 && argv[1].as.string ? argv[1].as.string : "";
            size_t searchlen = strlen(search), repllen = strlen(repl);
            if (searchlen == 0) { free(key); return js_clone_value(recv); }
            /* Build result by scanning for occurrences */
            size_t cap = slen + 64; char* r = (char*)malloc(cap + 1); size_t rpos = 0;
            const char* p = s;
            while (*p) {
                const char* found = strstr(p, search);
                if (!found) { size_t rem = strlen(p); if (rpos + rem >= cap) { cap = rpos + rem + 64; r = (char*)realloc(r, cap + 1); } memcpy(r + rpos, p, rem); rpos += rem; break; }
                size_t before = (size_t)(found - p);
                if (rpos + before + repllen >= cap) { cap = rpos + before + repllen + 64; r = (char*)realloc(r, cap + 1); }
                memcpy(r + rpos, p, before); rpos += before;
                memcpy(r + rpos, repl, repllen); rpos += repllen;
                p = found + searchlen;
                if (!all) { size_t rem = strlen(p); if (rpos + rem >= cap) { cap = rpos + rem + 64; r = (char*)realloc(r, cap + 1); } memcpy(r + rpos, p, rem); rpos += rem; break; }
            }
            r[rpos] = '\0';
            JsValue v = js_str(r); free(r); free(key); return v;
        }
        if (strcmp(key, "split") == 0) {
            /* Returns a JS array-like object with numeric keys */
            JsValue arr = js_obj_new();
            if (argc == 0 || (argv[0].tag == JS_UNDEFINED)) {
                js_obj_set(&arr, "0", js_clone_value(recv));
                js_obj_set(&arr, "length", JS_NUM(1));
                free(key); return arr;
            }
            const char* sep = argv[0].as.string ? argv[0].as.string : "";
            size_t seplen = strlen(sep);
            int count = 0;
            if (seplen == 0) {
                for (size_t i = 0; i < slen; ++i) {
                    char buf[2] = { s[i], '\0' };
                    char idx_s[16]; snprintf(idx_s, sizeof(idx_s), "%d", count++);
                    js_obj_set(&arr, idx_s, js_str(buf));
                }
            } else {
                const char* p = s;
                while (1) {
                    const char* found = strstr(p, sep);
                    const char* end = found ? found : (s + slen);
                    size_t n = (size_t)(end - p);
                    char* part = (char*)malloc(n + 1); memcpy(part, p, n); part[n] = '\0';
                    char idx_s[16]; snprintf(idx_s, sizeof(idx_s), "%d", count++);
                    js_obj_set(&arr, idx_s, js_str(part)); free(part);
                    if (!found) break;
                    p = found + seplen;
                }
            }
            js_obj_set(&arr, "length", JS_NUM(count));
            free(key); return arr;
        }
        if (strcmp(key, "concat") == 0) {
            size_t total = slen;
            for (int i = 0; i < argc; ++i) { char* t = js_to_cstr(argv[i]); total += strlen(t ? t : ""); free(t); }
            char* r = (char*)malloc(total + 1); memcpy(r, s, slen); size_t pos = slen;
            for (int i = 0; i < argc; ++i) { char* t = js_to_cstr(argv[i]); size_t tl = t ? strlen(t) : 0; memcpy(r + pos, t ? t : "", tl); pos += tl; free(t); }
            r[pos] = '\0';
            JsValue v = js_str(r); free(r); free(key); return v;
        }
        if (strcmp(key, "toString") == 0 || strcmp(key, "valueOf") == 0) {
            free(key); return js_clone_value(recv);
        }
        /* Unknown string method - fall through to return undefined */
        free(key); return JS_UNDEFINED();
    }
    /* ── End string methods ─────────────────────────────────────────────────── */

    if (recv.tag == JS_BYTES &&
        strcmp(key ? key : "", "toString") == 0) {
        JsValue enc = argc > 0 ? argv[0] : JS_UNDEFINED();
        JsValue r = js_buffer_toString(recv, enc);
        free(key);
        return r;
    }
    if (recv.tag == JS_BYTES && recv.as.bytes) {
        if (strcmp(key ? key : "", "slice") == 0) {
            JsValue r = js_buffer_slice(recv, argc > 0 ? argv[0] : JS_UNDEFINED(),
                                        argc > 1 ? argv[1] : JS_UNDEFINED());
            free(key);
            return r;
        }
        if (strcmp(key ? key : "", "readUIntBE") == 0) {
            JsValue r = js_buffer_readUIntBE(
                recv, argc > 0 ? argv[0] : JS_UNDEFINED(),
                argc > 1 ? argv[1] : JS_UNDEFINED());
            free(key);
            return r;
        }
        if (strcmp(key ? key : "", "compare") == 0) {
            JsValue r = js_buffer_compare(recv, argc > 0 ? argv[0] : JS_UNDEFINED());
            free(key);
            return r;
        }
        if (strcmp(key ? key : "", "copy") == 0) {
            JsValue r = js_buffer_copy(
                recv, argc > 0 ? argv[0] : JS_UNDEFINED(),
                argc > 1 ? argv[1] : JS_UNDEFINED(),
                argc > 2 ? argv[2] : JS_UNDEFINED(),
                argc > 3 ? argv[3] : JS_UNDEFINED());
            free(key);
            return r;
        }
    }

    /* ── Function methods: bind, call, apply ────────────────────────────────── */
    if (recv.tag == JS_FUNCTION) {
        if (strcmp(key, "bind") == 0) {
            /* Simplified: ignore 'this' binding, return the function itself */
            free(key); return js_clone_value(recv);
        }
        if (strcmp(key, "call") == 0 || strcmp(key, "apply") == 0) {
            /* call(thisArg, ...args) / apply(thisArg, argsArray) */
            /* For AOT purposes, ignore thisArg; just call with remaining args */
            if (argc <= 1) { free(key); return js_call_argv(recv, 0, NULL); }
            if (strcmp(key, "call") == 0) {
                free(key); return js_call_argv(recv, argc - 1, argv + 1);
            }
            /* apply: argv[1] is an array-like of args */
            if (argv[1].tag == JS_OBJECT && argv[1].as.obj) {
                int an = (int)js_to_num(js_obj_get_q(argv[1], "length"));
                JsValue tmp[JS_OBJ_MAX_PROPS];
                int n = an < JS_OBJ_MAX_PROPS ? an : JS_OBJ_MAX_PROPS;
                for (int i = 0; i < n; i++)
                    tmp[i] = js_obj_get(argv[1], JS_NUM((double)i));
                JsValue r = js_call_argv(recv, n, tmp);
                for (int i = 0; i < n; i++) js_dispose_value(&tmp[i]);
                free(key); return r;
            }
            free(key); return js_call_argv(recv, 0, NULL);
        }
        if (strcmp(key, "prototype") == 0) {
            /* handled in js_fn_get_or_create_proto via js_obj_get_q */
        }
    }

    /* ── Array-like object methods (only for objects with a numeric "length") ── */
    if (recv.tag == JS_OBJECT && recv.as.obj) {
        JsValue __lv = js_obj_get_q(recv, "length");
        int __hasLen = (__lv.tag == JS_NUMBER);
        int recvLen  = __hasLen ? (int)__lv.as.number : 0;
        if (__hasLen) {
        char __nk[24];

        if (strcmp(key, "concat") == 0) {
            JsValue r = js_obj_new(); int ri = 0;
            for (int i = 0; i < recvLen; i++) {
                snprintf(__nk, 24, "%d", ri++);
                js_obj_set(&r, __nk, js_obj_get(recv, JS_NUM((double)i)));
            }
            for (int ai = 0; ai < argc; ai++) {
                int al = (int)js_to_num(js_obj_get_q(argv[ai], "length"));
                if (argv[ai].tag == JS_OBJECT && al > 0) {
                    for (int i = 0; i < al; i++) {
                        snprintf(__nk, 24, "%d", ri++);
                        js_obj_set(&r, __nk, js_obj_get(argv[ai], JS_NUM((double)i)));
                    }
                } else if (argv[ai].tag != JS_UNDEFINED && argv[ai].tag != JS_NULL) {
                    snprintf(__nk, 24, "%d", ri++);
                    js_obj_set(&r, __nk, js_clone_value(argv[ai]));
                }
            }
            js_obj_set(&r, "length", JS_NUM((double)ri));
            free(key); return r;
        }
        if (strcmp(key, "push") == 0) {
            for (int ai = 0; ai < argc; ai++) {
                snprintf(__nk, 24, "%d", recvLen++);
                js_obj_set(&recv, __nk, js_clone_value(argv[ai]));
            }
            js_obj_set(&recv, "length", JS_NUM((double)recvLen));
            free(key); return JS_NUM((double)recvLen);
        }
        if (strcmp(key, "pop") == 0) {
            if (recvLen <= 0) { free(key); return JS_UNDEFINED(); }
            recvLen--;
            snprintf(__nk, 24, "%d", recvLen);
            JsValue v = js_obj_get(recv, JS_STR(__nk));
            js_obj_set(&recv, __nk, JS_UNDEFINED());
            js_obj_set(&recv, "length", JS_NUM((double)recvLen));
            free(key); return v;
        }
        if (strcmp(key, "shift") == 0) {
            if (recvLen <= 0) { free(key); return JS_UNDEFINED(); }
            JsValue v = js_obj_get(recv, JS_NUM(0.0));
            for (int i = 1; i < recvLen; i++) {
                snprintf(__nk, 24, "%d", i - 1);
                JsValue e = js_obj_get(recv, JS_NUM((double)i)); js_obj_set(&recv, __nk, e);
            }
            snprintf(__nk, 24, "%d", --recvLen);
            js_obj_set(&recv, __nk, JS_UNDEFINED());
            js_obj_set(&recv, "length", JS_NUM((double)recvLen));
            free(key); return v;
        }
        if (strcmp(key, "unshift") == 0) {
            for (int i = recvLen - 1; i >= 0; i--) {
                snprintf(__nk, 24, "%d", i + argc);
                JsValue e = js_obj_get(recv, JS_NUM((double)i)); js_obj_set(&recv, __nk, e);
            }
            for (int ai = 0; ai < argc; ai++) {
                snprintf(__nk, 24, "%d", ai);
                js_obj_set(&recv, __nk, js_clone_value(argv[ai]));
            }
            recvLen += argc;
            js_obj_set(&recv, "length", JS_NUM((double)recvLen));
            free(key); return JS_NUM((double)recvLen);
        }
        if (strcmp(key, "join") == 0) {
            char* sep = argc > 0 ? js_to_cstr(argv[0]) : js_strdup(",");
            char* result = js_strdup("");
            for (int i = 0; i < recvLen; i++) {
                JsValue e = js_obj_get(recv, JS_NUM((double)i));
                char* es = js_to_cstr(e); js_dispose_value(&e);
                size_t rlen = strlen(result), elen = strlen(es ? es : "");
                size_t slen = (i > 0 && sep) ? strlen(sep) : 0;
                char* nr = (char*)malloc(rlen + slen + elen + 1);
                if (nr) {
                    memcpy(nr, result, rlen);
                    if (i > 0 && sep) memcpy(nr + rlen, sep, slen);
                    memcpy(nr + rlen + slen, es ? es : "", elen);
                    nr[rlen + slen + elen] = '\0';
                }
                free(result); result = nr ? nr : js_strdup("");
                free(es);
            }
            free(sep);
            JsValue r = js_str(result); free(result);
            free(key); return r;
        }
        if (strcmp(key, "reverse") == 0) {
            for (int i = 0, j = recvLen - 1; i < j; i++, j--) {
                JsValue a2 = js_obj_get(recv, JS_NUM((double)i));
                JsValue b2 = js_obj_get(recv, JS_NUM((double)j));
                snprintf(__nk, 24, "%d", i); js_obj_set(&recv, __nk, b2);
                snprintf(__nk, 24, "%d", j); js_obj_set(&recv, __nk, a2);
            }
            free(key); return js_clone_value(recv);
        }
        if (strcmp(key, "indexOf") == 0) {
            if (argc == 0) { free(key); return JS_NUM(-1); }
            int start = argc > 1 ? (int)js_to_num(argv[1]) : 0;
            if (start < 0) start = recvLen + start;
            if (start < 0) start = 0;
            for (int i = start; i < recvLen; i++) {
                JsValue e = js_obj_get(recv, JS_NUM((double)i));
                int eq = js_truthy(js_eq(e, argv[0])); js_dispose_value(&e);
                if (eq) { free(key); return JS_NUM((double)i); }
            }
            free(key); return JS_NUM(-1);
        }
        if (strcmp(key, "includes") == 0) {
            if (argc == 0) { free(key); return JS_BOOL(0); }
            for (int i = 0; i < recvLen; i++) {
                JsValue e = js_obj_get(recv, JS_NUM((double)i));
                int eq = js_truthy(js_eq(e, argv[0])); js_dispose_value(&e);
                if (eq) { free(key); return JS_BOOL(1); }
            }
            free(key); return JS_BOOL(0);
        }
        if (strcmp(key, "slice") == 0) {
            int s = argc > 0 ? (int)js_to_num(argv[0]) : 0;
            int e = argc > 1 ? (int)js_to_num(argv[1]) : recvLen;
            if (s < 0) s = recvLen + s;
            if (s < 0) s = 0;
            if (e < 0) e = recvLen + e;
            if (e > recvLen) e = recvLen;
            JsValue r = js_obj_new(); int ri = 0;
            for (int i = s; i < e; i++) {
                snprintf(__nk, 24, "%d", ri++);
                js_obj_set(&r, __nk, js_obj_get(recv, JS_NUM((double)i)));
            }
            js_obj_set(&r, "length", JS_NUM((double)ri));
            free(key); return r;
        }
        if ((strcmp(key, "forEach") == 0 || strcmp(key, "map") == 0 ||
             strcmp(key, "filter") == 0 || strcmp(key, "find") == 0 ||
             strcmp(key, "findIndex") == 0 || strcmp(key, "some") == 0 ||
             strcmp(key, "every") == 0) && argc > 0) {
            int do_map     = strcmp(key, "map")       == 0;
            int do_filter  = strcmp(key, "filter")    == 0;
            int do_find    = strcmp(key, "find")      == 0;
            int do_fidx    = strcmp(key, "findIndex") == 0;
            int do_some    = strcmp(key, "some")      == 0;
            int do_every   = strcmp(key, "every")     == 0;
            JsValue fn = argv[0];
            JsValue result = (do_map || do_filter) ? js_obj_new() : JS_UNDEFINED();
            int ri = 0;
            for (int i = 0; i < recvLen; i++) {
                JsValue elem = js_obj_get(recv, JS_NUM((double)i));
                JsValue idx  = JS_NUM((double)i);
                JsValue __cba[] = {elem, idx, recv};
                JsValue cbr  = js_call_argv(fn, 3, __cba);
                if (do_map) {
                    snprintf(__nk, 24, "%d", ri++);
                    js_obj_set(&result, __nk, cbr);
                } else if (do_filter) {
                    if (js_truthy(cbr)) {
                        snprintf(__nk, 24, "%d", ri++);
                        js_obj_set(&result, __nk, js_clone_value(elem));
                        js_dispose_value(&cbr);
                    } else { js_dispose_value(&cbr); }
                } else if (do_find) {
                    if (js_truthy(cbr)) {
                        js_dispose_value(&result); result = js_clone_value(elem);
                        js_dispose_value(&cbr); js_dispose_value(&elem); js_dispose_value(&idx); break;
                    }
                    js_dispose_value(&cbr);
                } else if (do_fidx) {
                    if (js_truthy(cbr)) {
                        js_dispose_value(&result); result = js_clone_value(idx);
                        js_dispose_value(&cbr); js_dispose_value(&elem); js_dispose_value(&idx); break;
                    }
                    js_dispose_value(&cbr);
                } else if (do_some) {
                    if (js_truthy(cbr)) {
                        js_dispose_value(&result); result = JS_BOOL(1);
                        js_dispose_value(&cbr); js_dispose_value(&elem); js_dispose_value(&idx); break;
                    }
                    js_dispose_value(&cbr);
                } else if (do_every) {
                    if (!js_truthy(cbr)) {
                        js_dispose_value(&result); result = JS_BOOL(0);
                        js_dispose_value(&cbr); js_dispose_value(&elem); js_dispose_value(&idx); break;
                    }
                    js_dispose_value(&cbr);
                } else {
                    js_dispose_value(&cbr); /* forEach */
                }
                js_dispose_value(&elem); js_dispose_value(&idx);
            }
            if (do_map || do_filter) {
                snprintf(__nk, 24, "%d", ri);
                js_obj_set(&result, "length", JS_NUM((double)ri));
            } else if (do_find) {
                /* result already set or remains undefined */
            } else if (do_fidx && result.tag == JS_UNDEFINED) {
                result = JS_NUM(-1);
            } else if (do_some && result.tag == JS_UNDEFINED) {
                result = JS_BOOL(0);
            } else if (do_every && result.tag == JS_UNDEFINED) {
                result = JS_BOOL(1);
            }
            free(key); return result;
        }
        if (strcmp(key, "reduce") == 0 && argc > 0) {
            JsValue fn = argv[0];
            JsValue acc;
            int start = 0;
            if (argc > 1) { acc = js_clone_value(argv[1]); }
            else if (recvLen > 0) { acc = js_obj_get(recv, JS_NUM(0.0)); start = 1; }
            else { free(key); return JS_UNDEFINED(); }
            for (int i = start; i < recvLen; i++) {
                JsValue elem = js_obj_get(recv, JS_NUM((double)i));
                JsValue idx  = JS_NUM((double)i);
                JsValue __rda[] = {acc, elem, idx, recv};
                JsValue na   = js_call_argv(fn, 4, __rda);
                js_dispose_value(&acc); js_dispose_value(&elem); js_dispose_value(&idx);
                acc = na;
            }
            free(key); return acc;
        }
        if (strcmp(key, "toString") == 0 && recvLen >= 0) {
            /* Fall through to join with "," */
            char* sep = js_strdup(",");
            char* result = js_strdup("");
            for (int i = 0; i < recvLen; i++) {
                JsValue e = js_obj_get(recv, JS_NUM((double)i));
                char* es = js_to_cstr(e); js_dispose_value(&e);
                size_t rlen = strlen(result), elen = strlen(es ? es : "");
                size_t slen = (i > 0) ? 1 : 0;
                char* nr = (char*)malloc(rlen + slen + elen + 1);
                if (nr) {
                    memcpy(nr, result, rlen);
                    if (i > 0) nr[rlen] = ',';
                    memcpy(nr + rlen + slen, es ? es : "", elen);
                    nr[rlen + slen + elen] = '\0';
                }
                free(result); result = nr ? nr : js_strdup("");
                free(es);
            }
            free(sep);
            JsValue r = js_str(result); free(result);
            free(key); return r;
        }
        } /* if (__hasLen) */
    } /* if (recv.tag == JS_OBJECT) */
    /* ── End array methods ──────────────────────────────────────────────────── */

    JsValue meth = js_obj_get(recv, methName);
    JsValue res;
    /* http.ServerResponse methods (_res_*_w) expect argv[0] == recv (this). */
    bool isHttpRes = false;
    if (recv.tag == JS_OBJECT && recv.as.obj && meth.tag == JS_FUNCTION && meth.as.func) {
        JsValue kkey = JS_STR("__kind");
        JsValue k = js_obj_get(recv, kkey);
        if (k.tag == JS_STRING && k.as.string &&
            (strcmp(k.as.string, "httpRes") == 0 || strcmp(k.as.string, "stat") == 0))
            isHttpRes = true;
        js_dispose_value(&k);
        js_dispose_value(&kkey);
    }

    if (isHttpRes) {
        JsValue stack[JS_OBJ_MAX_PROPS];
        int n = 1 + (argc < JS_OBJ_MAX_PROPS - 1 ? argc : JS_OBJ_MAX_PROPS - 1);
        stack[0] = recv;
        for (int i = 0; i < argc && i + 1 < JS_OBJ_MAX_PROPS; ++i)
            stack[i + 1] = argv[i];
        res = js_call_argv(meth, n, stack);
    } else {
        res = js_call_argv(meth, argc, argv ? argv : NULL);
    }

    js_dispose_value(&meth);
    js_dispose_value(&methName);
    free(key);
    return res;
}

static inline JsValue js_jsc_cm0(JsValue recv, JsValue methName) {
    return js_jsc_call_member(recv, methName, 0, NULL);
}
static inline JsValue js_jsc_cm1(JsValue recv, JsValue methName, JsValue a0) {
    JsValue argv[1] = { a0 };
    return js_jsc_call_member(recv, methName, 1, argv);
}
static inline JsValue js_jsc_cm2(JsValue recv, JsValue methName, JsValue a0, JsValue a1) {
    JsValue argv[2] = { a0, a1 };
    return js_jsc_call_member(recv, methName, 2, argv);
}
static inline JsValue js_jsc_cm3(JsValue recv, JsValue methName, JsValue a0,
                                 JsValue a1, JsValue a2) {
    JsValue argv[3] = { a0, a1, a2 };
    return js_jsc_call_member(recv, methName, 3, argv);
}
static inline JsValue js_jsc_cm4(JsValue recv, JsValue methName, JsValue a0, JsValue a1,
                                 JsValue a2, JsValue a3) {
    JsValue argv[4] = { a0, a1, a2, a3 };
    return js_jsc_call_member(recv, methName, 4, argv);
}
static inline JsValue js_jsc_cm5(JsValue recv, JsValue methName, JsValue a0, JsValue a1,
                                 JsValue a2, JsValue a3, JsValue a4) {
    JsValue argv[5] = { a0, a1, a2, a3, a4 };
    return js_jsc_call_member(recv, methName, 5, argv);
}
static inline JsValue js_jsc_cm6(JsValue recv, JsValue methName, JsValue a0, JsValue a1,
                                 JsValue a2, JsValue a3, JsValue a4, JsValue a5) {
    JsValue argv[6] = { a0, a1, a2, a3, a4, a5 };
    return js_jsc_call_member(recv, methName, 6, argv);
}
static inline JsValue js_jsc_cm7(JsValue recv, JsValue methName, JsValue a0, JsValue a1,
                                 JsValue a2, JsValue a3, JsValue a4, JsValue a5,
                                 JsValue a6) {
    JsValue argv[7] = { a0, a1, a2, a3, a4, a5, a6 };
    return js_jsc_call_member(recv, methName, 7, argv);
}
static inline JsValue js_jsc_cm8(JsValue recv, JsValue methName, JsValue a0, JsValue a1,
                                 JsValue a2, JsValue a3, JsValue a4, JsValue a5,
                                 JsValue a6, JsValue a7) {
    JsValue argv[8] = { a0, a1, a2, a3, a4, a5, a6, a7 };
    return js_jsc_call_member(recv, methName, 8, argv);
}

/* ── Blue.* native desktop facade (GTK WebView host where linked) ────────── */

static inline JsValue _blue_win_title_w(int argc, JsValue* argv) {
    char* s = argc > 0 ? js_to_cstr(argv[0]) : js_strdup("");
    jsc_blue_window_set_title(s ? s : "");
    free(s);
    return JS_UNDEFINED();
}
static inline JsValue _blue_win_size_w(int argc, JsValue* argv) {
    int w = argc > 0 ? (int)js_to_num(argv[0]) : 0;
    int h = argc > 1 ? (int)js_to_num(argv[1]) : 0;
    jsc_blue_window_set_size(w, h);
    return JS_UNDEFINED();
}
static inline JsValue _blue_win_center_w(int /*argc*/, JsValue* /*argv*/) {
    jsc_blue_window_center();
    return JS_UNDEFINED();
}
static inline JsValue _blue_win_frameless_w(int argc, JsValue* argv) {
    jsc_blue_window_set_frameless(argc > 0 && js_truthy(argv[0]));
    return JS_UNDEFINED();
}
static inline JsValue _blue_win_minimize_w(int, JsValue*) {
    jsc_blue_window_minimize();
    return JS_UNDEFINED();
}
static inline JsValue _blue_win_maximize_w(int, JsValue*) {
    jsc_blue_window_maximize();
    return JS_UNDEFINED();
}
static inline JsValue _blue_win_close_w(int, JsValue*) {
    jsc_blue_window_close();
    return JS_UNDEFINED();
}
static inline JsValue _blue_win_ontop_w(int argc, JsValue* argv) {
    jsc_blue_window_set_always_on_top(argc > 0 && js_truthy(argv[0]));
    return JS_UNDEFINED();
}

static inline JsValue _blue_diag_open_w(int argc, JsValue* argv) {
    char* title =
        argc > 0 ? js_to_cstr(argv[0]) : js_strdup("Open");
    char* pat =
        argc > 1 ? js_to_cstr(argv[1]) : js_strdup("");
    int mul = argc > 2 ? (js_truthy(argv[2]) ? 1 : 0) : 0;
    char* paths =
        jsc_blue_dialog_open_files(title, mul, "", pat);
    JsValue rv = paths ? JS_STR(paths) : JS_STR("");
    free(paths);
    free(pat);
    free(title);
    return rv;
}

static inline JsValue _blue_diag_save_w(int argc, JsValue* argv) {
    char* title = argc > 0 ? js_to_cstr(argv[0]) : js_strdup("Save");
    char* def   = argc > 1 ? js_to_cstr(argv[1]) : js_strdup("");
    char* p     =
        jsc_blue_dialog_save_file(title, def, "");
    JsValue rv = p ? JS_STR(p) : JS_STR("");
    free(p);
    free(def);
    free(title);
    return rv;
}

static inline JsValue _blue_diag_msg_w(int argc, JsValue* argv) {
    char* title =
        argc > 0 ? js_to_cstr(argv[0]) : js_strdup("Blue");
    char* msg = argc > 1 ? js_to_cstr(argv[1]) : js_strdup("");
    char* typ = argc > 2 ? js_to_cstr(argv[2]) : js_strdup("info");
    char* btns =
        argc > 3 ? js_to_cstr(argv[3]) : js_strdup("OK");
    int idx = jsc_blue_dialog_message_box(title, msg, typ, btns);
    free(btns);
    free(typ);
    free(msg);
    free(title);
    return JS_NUM((double)idx);
}

static inline JsValue _blue_clip_write_w(int argc, JsValue* argv) {
    char* s = argc > 0 ? js_to_cstr(argv[0]) : js_strdup("");
    jsc_blue_clipboard_write_text(s ? s : "");
    free(s);
    return JS_UNDEFINED();
}

static inline JsValue _blue_clip_read_w(int /*argc*/, JsValue* /*argv*/) {
    char* t = jsc_blue_clipboard_read_text();
    JsValue rv = JS_STR(t ? t : "");
    free(t);
    return rv;
}

static inline JsValue _blue_proc_exec_w(int argc, JsValue* argv) {
    char* cmd =
        argc > 0 ? js_to_cstr(argv[0]) : js_strdup("");
    BlueExecResult* r = jsc_blue_process_exec(cmd);
    free(cmd);
    JsValue o = js_obj_new();
    if (!r) {
        js_obj_set(&o, "code", JS_NUM(-1.0));
        js_obj_set(&o, "stdout", JS_STR(""));
        js_obj_set(&o, "stderr", JS_STR(""));
        return o;
    }
    js_obj_set(&o, "code", JS_NUM((double)r->exit_code));
    js_obj_set(&o, "stdout",
               JS_STR(r->stdout_txt ? r->stdout_txt : ""));
    js_obj_set(&o, "stderr",
               JS_STR(r->stderr_txt ? r->stderr_txt : ""));
    jsc_blue_exec_result_free(r);
    return o;
}

static inline JsValue _blue_sys_mem_w(int, JsValue*) {
    char* j = jsc_blue_system_memory_json();
    JsValue v = js_obj_new();
    if (j) {
        if (strstr(j, "\"supported\":true")) {
            js_obj_set(&v, "supported", JS_BOOL(1));
            char* p = strstr(j, "\"memAvailableKb\":");
            if (p) js_obj_set(&v, "memAvailableKb", JS_NUM(atof(p + 17)));
        } else {
            js_obj_set(&v, "supported", JS_BOOL(0));
        }
        free(j);
    }
    return v;
}

static inline JsValue _blue_sys_cpu_w(int, JsValue*) {
    char* j = jsc_blue_system_cpu_json();
    JsValue v = js_obj_new();
    if (j) {
        if (strstr(j, "\"supported\":true")) {
            js_obj_set(&v, "supported", JS_BOOL(1));
        } else {
            js_obj_set(&v, "supported", JS_BOOL(0));
        }
        free(j);
    }
    return v;
}

/* Native plugin bridge for AOT output. Uses the same public C ABI as QuickJS. */

#define BLUE_AOT_PLUGIN_MAX_FUNCTIONS 64
#define BLUE_AOT_PLUGIN_MAX_HANDLES 32

typedef struct BlueAotPluginFunction {
    BluePluginFunction fn;
    void* userdata;
} BlueAotPluginFunction;

static BlueAotPluginFunction blue_aot_plugin_functions[BLUE_AOT_PLUGIN_MAX_FUNCTIONS];
static int blue_aot_plugin_function_count = 0;
static JsValue* blue_aot_plugin_blue_root = NULL;
static JsValue* blue_aot_plugin_global_root = NULL;
static JsValue blue_aot_plugin_blue_storage;
static int blue_aot_plugin_blue_storage_ready = 0;
static JsValue blue_aot_plugin_global_storage;
static int blue_aot_plugin_global_storage_ready = 0;

#if defined(_WIN32)
static HMODULE blue_aot_plugin_handles[BLUE_AOT_PLUGIN_MAX_HANDLES];
#else
static void* blue_aot_plugin_handles[BLUE_AOT_PLUGIN_MAX_HANDLES];
#endif
static int blue_aot_plugin_handle_count = 0;

static inline BluePluginValue blue_aot_make_undefined(void) {
    BluePluginValue v = {BLUE_PLUGIN_UNDEFINED, 0.0, 0, NULL, 0};
    return v;
}

static inline BluePluginValue blue_aot_make_null(void) {
    BluePluginValue v = {BLUE_PLUGIN_NULL, 0.0, 0, NULL, 0};
    return v;
}

static inline BluePluginValue blue_aot_make_bool(int value) {
    BluePluginValue v = {BLUE_PLUGIN_BOOL, 0.0, value ? 1 : 0, NULL, 0};
    return v;
}

static inline BluePluginValue blue_aot_make_number(double value) {
    BluePluginValue v = {BLUE_PLUGIN_NUMBER, value, 0, NULL, 0};
    return v;
}

static inline BluePluginValue blue_aot_make_string(const char* value) {
    BluePluginValue v = {BLUE_PLUGIN_STRING, 0.0, 0, value ? value : "",
                         value ? strlen(value) : 0};
    return v;
}

static inline BluePluginValue blue_aot_make_error(const char* message) {
    BluePluginValue v = {BLUE_PLUGIN_ERROR, 0.0, 0,
                         message ? message : "plugin error",
                         message ? strlen(message) : 12};
    return v;
}

static inline void blue_aot_plugin_log(const char* message) {
    fprintf(stderr, "blue plugin: %s\n", message ? message : "");
}

static inline BluePluginValue blue_aot_js_to_plugin_value(JsValue value) {
    switch (value.tag) {
        case JS_UNDEFINED:
            return blue_aot_make_undefined();
        case JS_NULL:
            return blue_aot_make_null();
        case JS_BOOLEAN:
            return blue_aot_make_bool(value.as.boolean);
        case JS_NUMBER:
            return blue_aot_make_number(value.as.number);
        case JS_STRING:
            return blue_aot_make_string(value.as.string ? value.as.string : "");
        default:
            return blue_aot_make_undefined();
    }
}

static inline JsValue blue_aot_plugin_to_js_value(BluePluginValue value) {
    switch (value.type) {
        case BLUE_PLUGIN_UNDEFINED:
            return JS_UNDEFINED();
        case BLUE_PLUGIN_NULL:
            return JS_NULL();
        case BLUE_PLUGIN_BOOL:
            return JS_BOOL(value.boolean ? 1 : 0);
        case BLUE_PLUGIN_NUMBER:
            return JS_NUM(value.number);
        case BLUE_PLUGIN_STRING:
            return JS_STR(value.string ? value.string : "");
        case BLUE_PLUGIN_ERROR:
            fprintf(stderr, "blue plugin error: %s\n",
                    value.string ? value.string : "plugin error");
            return JS_UNDEFINED();
    }
    return JS_UNDEFINED();
}

static inline JsValue blue_aot_plugin_call_index(int index, int argc, JsValue* argv) {
    if (index < 0 || index >= blue_aot_plugin_function_count ||
        !blue_aot_plugin_functions[index].fn)
        return JS_UNDEFINED();
    BluePluginValue args[JS_OBJ_MAX_PROPS];
    int n = argc < JS_OBJ_MAX_PROPS ? argc : JS_OBJ_MAX_PROPS;
    for (int i = 0; i < n; ++i)
        args[i] = blue_aot_js_to_plugin_value(argv[i]);
    BluePluginCallContext ctx = {blue_aot_plugin_functions[index].userdata};
    BluePluginValue out = blue_aot_plugin_functions[index].fn(&ctx, n, n ? args : NULL);
    return blue_aot_plugin_to_js_value(out);
}

#define BLUE_AOT_PLUGIN_THUNK(N) \
    static inline JsValue blue_aot_plugin_thunk_##N(int argc, JsValue* argv) { \
        return blue_aot_plugin_call_index(N, argc, argv); \
    }
BLUE_AOT_PLUGIN_THUNK(0)  BLUE_AOT_PLUGIN_THUNK(1)
BLUE_AOT_PLUGIN_THUNK(2)  BLUE_AOT_PLUGIN_THUNK(3)
BLUE_AOT_PLUGIN_THUNK(4)  BLUE_AOT_PLUGIN_THUNK(5)
BLUE_AOT_PLUGIN_THUNK(6)  BLUE_AOT_PLUGIN_THUNK(7)
BLUE_AOT_PLUGIN_THUNK(8)  BLUE_AOT_PLUGIN_THUNK(9)
BLUE_AOT_PLUGIN_THUNK(10) BLUE_AOT_PLUGIN_THUNK(11)
BLUE_AOT_PLUGIN_THUNK(12) BLUE_AOT_PLUGIN_THUNK(13)
BLUE_AOT_PLUGIN_THUNK(14) BLUE_AOT_PLUGIN_THUNK(15)
BLUE_AOT_PLUGIN_THUNK(16) BLUE_AOT_PLUGIN_THUNK(17)
BLUE_AOT_PLUGIN_THUNK(18) BLUE_AOT_PLUGIN_THUNK(19)
BLUE_AOT_PLUGIN_THUNK(20) BLUE_AOT_PLUGIN_THUNK(21)
BLUE_AOT_PLUGIN_THUNK(22) BLUE_AOT_PLUGIN_THUNK(23)
BLUE_AOT_PLUGIN_THUNK(24) BLUE_AOT_PLUGIN_THUNK(25)
BLUE_AOT_PLUGIN_THUNK(26) BLUE_AOT_PLUGIN_THUNK(27)
BLUE_AOT_PLUGIN_THUNK(28) BLUE_AOT_PLUGIN_THUNK(29)
BLUE_AOT_PLUGIN_THUNK(30) BLUE_AOT_PLUGIN_THUNK(31)
#undef BLUE_AOT_PLUGIN_THUNK

static inline JsFnPtr blue_aot_plugin_thunk_for_index(int index) {
    static JsFnPtr thunks[] = {
        blue_aot_plugin_thunk_0,  blue_aot_plugin_thunk_1,
        blue_aot_plugin_thunk_2,  blue_aot_plugin_thunk_3,
        blue_aot_plugin_thunk_4,  blue_aot_plugin_thunk_5,
        blue_aot_plugin_thunk_6,  blue_aot_plugin_thunk_7,
        blue_aot_plugin_thunk_8,  blue_aot_plugin_thunk_9,
        blue_aot_plugin_thunk_10, blue_aot_plugin_thunk_11,
        blue_aot_plugin_thunk_12, blue_aot_plugin_thunk_13,
        blue_aot_plugin_thunk_14, blue_aot_plugin_thunk_15,
        blue_aot_plugin_thunk_16, blue_aot_plugin_thunk_17,
        blue_aot_plugin_thunk_18, blue_aot_plugin_thunk_19,
        blue_aot_plugin_thunk_20, blue_aot_plugin_thunk_21,
        blue_aot_plugin_thunk_22, blue_aot_plugin_thunk_23,
        blue_aot_plugin_thunk_24, blue_aot_plugin_thunk_25,
        blue_aot_plugin_thunk_26, blue_aot_plugin_thunk_27,
        blue_aot_plugin_thunk_28, blue_aot_plugin_thunk_29,
        blue_aot_plugin_thunk_30, blue_aot_plugin_thunk_31
    };
    int count = (int)(sizeof(thunks) / sizeof(thunks[0]));
    return (index >= 0 && index < count) ? thunks[index] : NULL;
}

static inline void blue_aot_define_plugin_function(const char* namespace_name,
                                                   const char* function_name,
                                                   BluePluginFunction fn,
                                                   int arity,
                                                   void* userdata) {
    (void)arity;
    JsValue* root = blue_aot_plugin_global_root ? blue_aot_plugin_global_root
                                                : blue_aot_plugin_blue_root;
    if (!root || !namespace_name || !function_name || !fn)
        return;
    if (blue_aot_plugin_function_count >= 32)
        return;
    int index = blue_aot_plugin_function_count++;
    blue_aot_plugin_functions[index].fn = fn;
    blue_aot_plugin_functions[index].userdata = userdata;

    JsValue ns_key = JS_STR(namespace_name);
    JsValue ns = js_obj_get(*root, ns_key);
    js_dispose_value(&ns_key);
    if (ns.tag != JS_OBJECT) {
        js_dispose_value(&ns);
        ns = js_obj_new();
        js_obj_set(root, namespace_name, js_clone_value(ns));
    }
    JsFnPtr thunk = blue_aot_plugin_thunk_for_index(index);
    if (thunk)
        js_obj_set_fn(&ns, function_name, thunk);
    js_dispose_value(&ns);
}

static inline BluePluginHost* blue_aot_plugin_host(void) {
    static BluePluginHost host;
    static int initialized = 0;
    if (!initialized) {
        host.api_version = BLUE_PLUGIN_API_VERSION;
        host.define_function = blue_aot_define_plugin_function;
        host.make_undefined = blue_aot_make_undefined;
        host.make_null = blue_aot_make_null;
        host.make_bool = blue_aot_make_bool;
        host.make_number = blue_aot_make_number;
        host.make_string = blue_aot_make_string;
        host.make_error = blue_aot_make_error;
        host.log = blue_aot_plugin_log;
        initialized = 1;
    }
    return &host;
}

static inline int blue_aot_plugin_load_path(const char* path) {
    if (!path || !path[0])
        return 0;
#if defined(_WIN32)
    HMODULE handle = LoadLibraryA(path);
    if (!handle) {
        fprintf(stderr, "blue: plugin load failed: %s\n", path);
        return 0;
    }
    BluePluginInitFn init = (BluePluginInitFn)GetProcAddress(handle, "blue_plugin_init");
#else
    void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "blue: plugin load failed: %s: %s\n", path, dlerror());
        return 0;
    }
    BluePluginInitFn init = (BluePluginInitFn)dlsym(handle, "blue_plugin_init");
#endif
    if (!init) {
        fprintf(stderr, "blue: plugin missing blue_plugin_init: %s\n", path);
#if defined(_WIN32)
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        return 0;
    }
    if (!init(blue_aot_plugin_host())) {
        fprintf(stderr, "blue: plugin init failed: %s\n", path);
#if defined(_WIN32)
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        return 0;
    }
    if (blue_aot_plugin_handle_count < BLUE_AOT_PLUGIN_MAX_HANDLES)
        blue_aot_plugin_handles[blue_aot_plugin_handle_count++] = handle;
    return 1;
}

static inline JsValue _blue_plugin_load_w(int argc, JsValue* argv) {
    char* path = argc > 0 ? js_to_cstr(argv[0]) : js_strdup("");
    int ok = blue_aot_plugin_load_path(path);
    free(path);
    return JS_BOOL(ok ? 1 : 0);
}


#if defined(JSC_HYBRID)
/* AOT → QuickJS island: invoke __BLUE_ISLAND[exportName](payloadString). */
static inline JsValue _blue_call_island_w(int argc, JsValue* argv) {
    char* exportName =
        argc > 0 ? js_to_cstr(argv[0]) : js_strdup("");
    char* payload =
        argc > 1 ? js_to_cstr(argv[1]) : js_strdup("");
    JsValue out = jsc_hybrid_island_call_json(exportName, payload);
    free(exportName);
    free(payload);
    return out;
}
#endif

static inline JsValue js_blue_desktop_root(void) {
    JsValue blue = js_obj_new();

    JsValue win = js_obj_new();
    js_obj_set_fn(&win, "setTitle", _blue_win_title_w);
    js_obj_set_fn(&win, "setSize", _blue_win_size_w);
    js_obj_set_fn(&win, "center", _blue_win_center_w);
    js_obj_set_fn(&win, "setFrameless", _blue_win_frameless_w);
    js_obj_set_fn(&win, "minimize", _blue_win_minimize_w);
    js_obj_set_fn(&win, "maximize", _blue_win_maximize_w);
    js_obj_set_fn(&win, "close", _blue_win_close_w);
    js_obj_set_fn(&win, "setAlwaysOnTop", _blue_win_ontop_w);
    js_obj_set(&blue, "Window", win);

    JsValue dlg = js_obj_new();
    js_obj_set_fn(&dlg, "showOpenDialog", _blue_diag_open_w);
    js_obj_set_fn(&dlg, "showSaveDialog", _blue_diag_save_w);
    js_obj_set_fn(&dlg, "showMessageBox", _blue_diag_msg_w);
    js_obj_set(&blue, "Dialog", dlg);

    JsValue clip = js_obj_new();
    js_obj_set_fn(&clip, "writeText", _blue_clip_write_w);
    js_obj_set_fn(&clip, "readText", _blue_clip_read_w);
    js_obj_set(&blue, "Clipboard", clip);

    JsValue proc = js_obj_new();
    js_obj_set_fn(&proc, "exec", _blue_proc_exec_w);
    js_obj_set(&blue, "Process", proc);

    JsValue sys = js_obj_new();
    js_obj_set_fn(&sys, "getMemoryInfo", _blue_sys_mem_w);
    js_obj_set_fn(&sys, "getCPU", _blue_sys_cpu_w);
    js_obj_set(&blue, "System", sys);

    JsValue plugin = js_obj_new();
    js_obj_set_fn(&plugin, "load", _blue_plugin_load_w);
    js_obj_set(&blue, "Plugin", plugin);

#if defined(JSC_HYBRID)
    js_obj_set_fn(&blue, "callIsland", _blue_call_island_w);
#endif

    if (blue_aot_plugin_blue_storage_ready)
        js_dispose_value(&blue_aot_plugin_blue_storage);
    blue_aot_plugin_blue_storage = blue;
    blue_aot_plugin_blue_storage_ready = 1;
    blue_aot_plugin_blue_root = &blue_aot_plugin_blue_storage;
    return js_clone_value(blue_aot_plugin_blue_storage);
}

static inline JsValue _js_json_stringify_stub(int argc, JsValue* argv) {
    if (argc <= 0) return js_str("undefined");
    char* s = js_to_cstr(argv[0]);
    JsValue out = js_str(s ? s : "undefined");
    free(s);
    return out;
}

static inline JsValue _js_json_parse_stub(int, JsValue*) {
    return js_obj_new();
}

/* ── Object built-in (for Babel class helpers: Object.create, Object.setPrototypeOf) */

static inline JsValue _js_object_create_w(int argc, JsValue* argv) {
    JsValue proto = argc > 0 ? argv[0] : (JsValue){JS_UNDEFINED, {0}};
    JsValue obj = js_obj_new();
    if (proto.tag == JS_OBJECT)
        js_obj_set(&obj, "__proto__", js_clone_value(proto));
    return obj;
}
static inline JsValue _js_object_setprototypeof_w(int argc, JsValue* argv) {
    if (argc < 2) return argc > 0 ? js_clone_value(argv[0]) : (JsValue){JS_UNDEFINED, {0}};
    js_obj_set(&argv[0], "__proto__", js_clone_value(argv[1]));
    return js_clone_value(argv[0]);
}
static inline JsValue _js_object_assign_w(int argc, JsValue* argv) {
    if (argc == 0) return (JsValue){JS_UNDEFINED, {0}};
    JsValue target = js_clone_value(argv[0]);
    for (int ai = 1; ai < argc; ai++) {
        if (argv[ai].tag != JS_OBJECT || !argv[ai].as.obj) continue;
        JsObject* src = argv[ai].as.obj;
        for (int i = 0; i < src->count; i++)
            js_obj_set(&target, src->props[i].key, js_clone_value(src->props[i].value));
    }
    return target;
}
static inline JsValue _js_object_keys_w(int argc, JsValue* argv) {
    JsValue arr = js_obj_new();
    if (argc == 0 || argv[0].tag != JS_OBJECT || !argv[0].as.obj) {
        js_obj_set(&arr, "length", JS_NUM(0)); return arr;
    }
    JsObject* o = argv[0].as.obj; int n = 0; char nk[24];
    for (int i = 0; i < o->count; i++) {
        if (strcmp(o->props[i].key, "__proto__") == 0) continue;
        snprintf(nk, 24, "%d", n++);
        js_obj_set(&arr, nk, js_str(o->props[i].key));
    }
    js_obj_set(&arr, "length", JS_NUM((double)n));
    return arr;
}
static inline JsValue _js_object_defineproperty_w(int argc, JsValue* argv) {
    /* Stub: for Babel class helpers that use defineProperty for method setup,
       we only care about simple value descriptors, not getters/setters. */
    if (argc < 3) return argc > 0 ? js_clone_value(argv[0]) : JS_UNDEFINED();
    if (argv[2].tag == JS_OBJECT && argv[2].as.obj) {
        JsObject* desc __attribute__((unused)) = argv[2].as.obj;
        JsValue val = js_obj_get_q(argv[2], "value");
        if (val.tag != JS_UNDEFINED && argv[1].tag == JS_STRING)
            js_obj_set(&argv[0], argv[1].as.string, js_clone_value(val));
        js_dispose_value(&val);
    }
    return argc > 0 ? js_clone_value(argv[0]) : JS_UNDEFINED();
}
static inline JsValue _js_object_getprototypeof_w(int argc, JsValue* argv) {
    if (argc == 0) return JS_NULL();
    return js_obj_get_q(argv[0], "__proto__");
}
static inline JsValue js_builtin_object(void) {
    JsValue o = js_obj_new();
    js_obj_set_fn(&o, "create",           _js_object_create_w);
    js_obj_set_fn(&o, "setPrototypeOf",   _js_object_setprototypeof_w);
    js_obj_set_fn(&o, "assign",           _js_object_assign_w);
    js_obj_set_fn(&o, "keys",             _js_object_keys_w);
    js_obj_set_fn(&o, "defineProperty",   _js_object_defineproperty_w);
    js_obj_set_fn(&o, "getPrototypeOf",   _js_object_getprototypeof_w);
    return o;
}

static inline JsValue js_jsc_init_global_this(JsValue jsc_console, JsValue jsc_process_val,
                                               JsValue jsc_buffer_ns) {
    JsValue gt = js_obj_new();
    JsValue mt = js_builtin_math();
    JsValue jt = js_obj_new();
    JsValue ot = js_builtin_object();
    js_obj_set_fn(&jt, "stringify", _js_json_stringify_stub);
    js_obj_set_fn(&jt, "parse",     _js_json_parse_stub);
    js_obj_set(&gt, "console", jsc_console);
    js_obj_set(&gt, "process", jsc_process_val);
    js_obj_set(&gt, "Buffer", jsc_buffer_ns);
    js_obj_set(&gt, "Math", mt);
    js_obj_set(&gt, "JSON", jt);
    js_obj_set(&gt, "Object", ot);
    js_obj_set(&gt, "Blue", js_blue_desktop_root());
    js_obj_set(&gt, "globalThis", gt);
    js_obj_set(&gt, "global", gt);
    if (blue_aot_plugin_global_storage_ready)
        js_dispose_value(&blue_aot_plugin_global_storage);
    blue_aot_plugin_global_storage = gt;
    blue_aot_plugin_global_storage_ready = 1;
    blue_aot_plugin_global_root = &blue_aot_plugin_global_storage;
    /* mt/jt/ot ownership transferred to gt via js_obj_set - do NOT dispose them */
    return js_clone_value(blue_aot_plugin_global_storage);
}

#endif /* JS_GLOBALS_H */
