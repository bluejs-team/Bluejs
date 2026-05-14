/*
 * js_node.h - Node.js built-in API stubs for blue-compiled binaries.
 *
 * Must be included before any system headers so that _POSIX_C_SOURCE takes
 * effect (enables POSIX extensions: realpath, getcwd, PATH_MAX, …).
 */
/* Enable POSIX.1-2008 extensions (realpath, getcwd, PATH_MAX, getpwuid…).
 * Defined before any system includes so it is effective everywhere. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

/*
 * js_node.h - Node.js built-in API stubs for blue-compiled binaries (cont.).
 *
 * Provides C99 implementations of the most common Node.js built-in modules:
 *   fs      readFileSync / writeFileSync / existsSync / readdirSync (stub)
 *   path    join / resolve / dirname / basename / extname
 *   os      platform / homedir / tmpdir
 *   process argv / env.X / exit / cwd
 *
 * Each module is returned by js_node_require(JS_STR("fs")) etc. as a JsValue
 * JS_OBJECT with JS_FUNCTION properties.  The emitter also generates direct
 * compile-time calls (js_fs_readFileSync, js_path_join, …) for known patterns
 * so there is zero runtime overhead on the happy path.
 *
 * Self-contained - requires only C99 + POSIX headers.
 */

#ifndef JS_NODE_H
#define JS_NODE_H

#include "js_value.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>

#include "js_globals.h"

extern char** environ;

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* ── Global process state (one definition in jsc_node_io.cpp) ─────────────── */

#ifdef __cplusplus
extern "C" {
#endif
extern int    jsc_argc;
extern char** jsc_argv;
#ifdef __cplusplus
}
#endif

/* Call this at the start of generated main(). */
static inline void js_process_init(int argc, char** argv) {
    jsc_argc = argc;
    jsc_argv = argv;
}

/* ════════════════════════════════════════════════════════════════════════════
 * fs module
 * ════════════════════════════════════════════════════════════════════════════ */

/*
 * fs.readFileSync(path[, encoding])
 * Returns file contents as a JsValue string.  Encoding argument is accepted
 * but ignored (output is always the raw text).  Returns JS_UNDEFINED on error.
 */
static inline JsValue js_fs_readFileSync(JsValue path, JsValue encoding) {
    (void)encoding;
    char* cpath = js_to_cstr(path);
    FILE* f = fopen(cpath, "rb");
    free(cpath);
    if (!f) return JS_UNDEFINED();

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return JS_UNDEFINED(); }
    long size = ftell(f);
    if (size < 0)                    { fclose(f); return JS_UNDEFINED(); }
    rewind(f);

    unsigned char* buf = (unsigned char*)malloc((size_t)size);
    if (!buf)                        { fclose(f); return JS_UNDEFINED(); }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);

    return js_byte_buffer_own(buf, got);
}

/*
 * fs.writeFileSync(path, data[, options])
 * Writes data (coerced to string) to path.  Returns JS_UNDEFINED.
 */
static inline JsValue js_fs_writeFileSync(JsValue path, JsValue data,
                                           JsValue opts) {
    (void)opts;
    char* cpath = js_to_cstr(path);
    char* cdata = js_to_cstr(data);
    FILE* f = fopen(cpath, "wb");
    if (f) { fputs(cdata, f); fclose(f); }
    free(cpath);
    free(cdata);
    return JS_UNDEFINED();
}

/*
 * fs.existsSync(path) → boolean
 */
static inline JsValue js_fs_existsSync(JsValue path) {
    char* cpath = js_to_cstr(path);
    struct stat st;
    int exists = (stat(cpath, &st) == 0);
    free(cpath);
    return JS_BOOL(exists);
}

/*
 * fs.readdirSync(path) → object with numeric string keys ("0","1",...) and
 * a "length" property.  Compatible with AOT index-based loops:
 *   var list = fs.readdirSync(dir);
 *   for (var i = 0; i < list.length; i++) { ... list[i] ... }
 * for...of and forEach are not supported in AOT; use the island for those.
 *
 * Note: the AOT property bag holds JS_OBJ_MAX_PROPS slots; "length" occupies
 * slot 0 and up to JS_OBJ_MAX_PROPS-1 file entries are stored.  Directories
 * with more entries than that are truncated (use the island for large dirs).
 */
static inline JsValue js_fs_readdirSync(JsValue path) {
    JsValue arr = js_obj_new();
    /* Reserve slot 0 for "length" so it is never displaced by entries. */
    js_obj_set(&arr, "length", JS_NUM(0));

    char* cpath = js_to_cstr(path);
    if (!cpath || !cpath[0]) { free(cpath); return arr; }

    DIR* d = opendir(cpath);
    free(cpath);
    if (!d) return arr;

    /* JS_OBJ_MAX_PROPS - 1 entries fit (slot 0 is "length"). */
    const int max_entries = JS_OBJ_MAX_PROPS - 1;
    int count = 0;
    struct dirent* ent;
    char idx[32];
    while ((ent = readdir(d)) != NULL && count < max_entries) {
        if (ent->d_name[0] == '.' &&
            (ent->d_name[1] == '\0' ||
             (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
            continue;
        snprintf(idx, sizeof(idx), "%d", count++);
        js_obj_set(&arr, idx, JS_STR(ent->d_name));
    }
    closedir(d);
    /* Overwrite the "length" slot with the actual count. */
    js_obj_set(&arr, "length", JS_NUM((double)count));
    return arr;
}

/* Wrappers for storing in the property bag (argc/argv-style call convention) */
static inline JsValue _js_fs_readFileSync_w(int argc, JsValue* argv) {
    JsValue p = argc > 0 ? argv[0] : JS_UNDEFINED();
    JsValue e = argc > 1 ? argv[1] : JS_UNDEFINED();
    return js_fs_readFileSync(p, e);
}
static inline JsValue _js_fs_writeFileSync_w(int argc, JsValue* argv) {
    JsValue p = argc > 0 ? argv[0] : JS_UNDEFINED();
    JsValue d = argc > 1 ? argv[1] : JS_UNDEFINED();
    JsValue o = argc > 2 ? argv[2] : JS_UNDEFINED();
    return js_fs_writeFileSync(p, d, o);
}
static inline JsValue _js_fs_existsSync_w(int argc, JsValue* argv) {
    return js_fs_existsSync(argc > 0 ? argv[0] : JS_UNDEFINED());
}
static inline JsValue _js_fs_readdirSync_w(int argc, JsValue* argv) {
    return js_fs_readdirSync(argc > 0 ? argv[0] : JS_UNDEFINED());
}

/*
 * fs.appendFileSync(path, data[, options])
 */
static inline JsValue js_fs_appendFileSync(JsValue path, JsValue data,
                                            JsValue opts) {
    (void)opts;
    char* cpath = js_to_cstr(path);
    char* cdata = js_to_cstr(data);
    FILE* f = fopen(cpath, "ab");
    if (f) { fputs(cdata, f); fclose(f); }
    free(cpath);
    free(cdata);
    return JS_UNDEFINED();
}

static inline JsValue _js_fs_appendFileSync_w(int argc, JsValue* argv) {
    JsValue p = argc > 0 ? argv[0] : JS_UNDEFINED();
    JsValue d = argc > 1 ? argv[1] : JS_UNDEFINED();
    JsValue o = argc > 2 ? argv[2] : JS_UNDEFINED();
    return js_fs_appendFileSync(p, d, o);
}

/* fs.unlinkSync(path) - delete a file */
static inline JsValue js_fs_unlinkSync(JsValue path) {
    char* p = js_to_cstr(path);
    unlink(p ? p : "");
    free(p);
    return JS_UNDEFINED();
}
static inline JsValue _js_fs_unlinkSync_w(int argc, JsValue* argv) {
    return js_fs_unlinkSync(argc > 0 ? argv[0] : JS_UNDEFINED());
}

/* fs.mkdirSync(path[, options]) - create directory, ignore EEXIST if recursive */
static inline JsValue js_fs_mkdirSync(JsValue path, JsValue opts) {
    char* p = js_to_cstr(path);
    (void)opts;
#ifdef _WIN32
    if (p) mkdir(p);
#else
    if (p) mkdir(p, 0755);
#endif
    free(p);
    return JS_UNDEFINED();
}
static inline JsValue _js_fs_mkdirSync_w(int argc, JsValue* argv) {
    return js_fs_mkdirSync(argc > 0 ? argv[0] : JS_UNDEFINED(),
                           argc > 1 ? argv[1] : JS_UNDEFINED());
}

/* fs.renameSync(oldPath, newPath) */
static inline JsValue js_fs_renameSync(JsValue oldp, JsValue newp) {
    char* op = js_to_cstr(oldp);
    char* np = js_to_cstr(newp);
    if (op && np) rename(op, np);
    free(op); free(np);
    return JS_UNDEFINED();
}
static inline JsValue _js_fs_renameSync_w(int argc, JsValue* argv) {
    return js_fs_renameSync(argc > 0 ? argv[0] : JS_UNDEFINED(),
                            argc > 1 ? argv[1] : JS_UNDEFINED());
}

/* fs.copyFileSync(src, dest) */
static inline JsValue js_fs_copyFileSync(JsValue src, JsValue dest) {
    char* sp = js_to_cstr(src);
    char* dp = js_to_cstr(dest);
    if (sp && dp) {
        FILE* in  = fopen(sp, "rb");
        FILE* out = in ? fopen(dp, "wb") : NULL;
        if (in && out) {
            char buf[8192]; size_t n;
            while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
        }
        if (in)  fclose(in);
        if (out) fclose(out);
    }
    free(sp); free(dp);
    return JS_UNDEFINED();
}
static inline JsValue _js_fs_copyFileSync_w(int argc, JsValue* argv) {
    return js_fs_copyFileSync(argc > 0 ? argv[0] : JS_UNDEFINED(),
                              argc > 1 ? argv[1] : JS_UNDEFINED());
}

/* isFile/isDirectory helpers - argv[0] is the stat object (self via __kind=stat dispatch) */
static inline JsValue _stat_isFile_w(int argc, JsValue* argv) {
    if (argc > 0) return js_obj_get_q(argv[0], "__isFile");
    return JS_BOOL(0);
}
static inline JsValue _stat_isDir_w(int argc, JsValue* argv) {
    if (argc > 0) return js_obj_get_q(argv[0], "__isDir");
    return JS_BOOL(0);
}

/* fs.statSync(path) → { size, mtimeMs, isFile(), isDirectory() } */
static inline JsValue js_fs_statSync(JsValue path) {
    char* p = js_to_cstr(path);
    struct stat st;
    if (!p || stat(p, &st) != 0) { free(p); return JS_UNDEFINED(); }
    free(p);
    JsValue obj = js_obj_new();
    js_obj_set(&obj, "__kind",   js_str("stat"));
    js_obj_set(&obj, "size",     JS_NUM((double)st.st_size));
    js_obj_set(&obj, "mtimeMs",  JS_NUM((double)st.st_mtime * 1000.0));
    js_obj_set(&obj, "__isFile", JS_BOOL(S_ISREG(st.st_mode) ? 1 : 0));
    js_obj_set(&obj, "__isDir",  JS_BOOL(S_ISDIR(st.st_mode) ? 1 : 0));
    js_obj_set_fn(&obj, "isFile",      _stat_isFile_w);
    js_obj_set_fn(&obj, "isDirectory", _stat_isDir_w);
    return obj;
}
static inline JsValue _js_fs_statSync_w(int argc, JsValue* argv) {
    return js_fs_statSync(argc > 0 ? argv[0] : JS_UNDEFINED());
}

/* Build and return the fs module object. */
static inline JsValue js_node_fs(void) {
    JsValue m = js_obj_new();
    js_obj_set_fn(&m, "readFileSync",   _js_fs_readFileSync_w);
    js_obj_set_fn(&m, "writeFileSync",  _js_fs_writeFileSync_w);
    js_obj_set_fn(&m, "appendFileSync", _js_fs_appendFileSync_w);
    js_obj_set_fn(&m, "existsSync",     _js_fs_existsSync_w);
    js_obj_set_fn(&m, "readdirSync",    _js_fs_readdirSync_w);
    js_obj_set_fn(&m, "unlinkSync",     _js_fs_unlinkSync_w);
    js_obj_set_fn(&m, "mkdirSync",      _js_fs_mkdirSync_w);
    js_obj_set_fn(&m, "renameSync",     _js_fs_renameSync_w);
    js_obj_set_fn(&m, "copyFileSync",   _js_fs_copyFileSync_w);
    js_obj_set_fn(&m, "statSync",       _js_fs_statSync_w);
    return m;
}

/* ════════════════════════════════════════════════════════════════════════════
 * path module
 * ════════════════════════════════════════════════════════════════════════════ */

/*
 * path.join(seg0, seg1, …)  - variadic, first arg is segment count.
 * Concatenates path segments with '/' separators.  Does not normalize
 * double-slashes or resolve '..', which matches Node.js basic behaviour for
 * simple cases.
 */
static inline JsValue js_path_join(int argc, ...) {
    char buf[4096];
    buf[0] = '\0';
    va_list ap;
    va_start(ap, argc);
    for (int i = 0; i < argc; i++) {
        JsValue seg = va_arg(ap, JsValue);
        char* s = js_to_cstr(seg);
        size_t blen = strlen(buf);
        size_t slen = strlen(s);
        if (blen > 0 && buf[blen - 1] != '/' && slen > 0 && s[0] != '/')
            strncat(buf, "/", sizeof(buf) - blen - 1);
        strncat(buf, s, sizeof(buf) - strlen(buf) - 1);
        free(s);
    }
    va_end(ap);
    return js_str(buf);
}

/*
 * path.resolve(p) → absolute path.
 * If p is already absolute, return it unchanged.
 * If relative, prepend the current working directory.
 */
static inline JsValue js_path_resolve(JsValue p) {
    char* cp = js_to_cstr(p);
    if (cp[0] == '/') {
        JsValue r = js_str(cp);
        free(cp);
        return r;
    }
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
    /* Join cwd + "/" + cp */
    size_t cwdlen = strlen(cwd);
    size_t cplen  = strlen(cp);
    char* buf = (char*)malloc(cwdlen + 1 + cplen + 1);
    memcpy(buf, cwd, cwdlen);
    buf[cwdlen] = '/';
    memcpy(buf + cwdlen + 1, cp, cplen + 1);
    free(cp);
    JsValue r = js_str(buf);
    free(buf);
    return r;
}

/*
 * path.dirname(p)  - everything up to the last '/'.
 */
static inline JsValue js_path_dirname(JsValue p) {
    char* cp = js_to_cstr(p);
    /* Find last '/' */
    char* last = strrchr(cp, '/');
    JsValue result;
    if (!last) {
        result = js_str(".");
    } else if (last == cp) {
        result = js_str("/");
    } else {
        *last = '\0';
        result = js_str(cp);
    }
    free(cp);
    return result;
}

/*
 * path.basename(p[, ext])  - filename component, optionally stripping ext.
 */
static inline JsValue js_path_basename(JsValue p, JsValue ext) {
    char* cp = js_to_cstr(p);
    char* base = strrchr(cp, '/');
    base = base ? base + 1 : cp;

    if (ext.tag == JS_STRING && ext.as.string && ext.as.string[0]) {
        size_t blen = strlen(base);
        size_t elen = strlen(ext.as.string);
        if (blen > elen &&
            strcmp(base + blen - elen, ext.as.string) == 0) {
            base[blen - elen] = '\0';
        }
    }
    JsValue result = js_str(base);
    free(cp);
    return result;
}

/*
 * path.extname(p)  - extension including the leading '.'.
 */
static inline JsValue js_path_extname(JsValue p) {
    char* cp = js_to_cstr(p);
    char* base = strrchr(cp, '/');
    base = base ? base + 1 : cp;
    char* dot = strrchr(base, '.');
    JsValue result = dot ? js_str(dot) : js_str("");
    free(cp);
    return result;
}

/* Wrappers for property bag storage */
static inline JsValue _js_path_join_w(int argc, JsValue* argv) {
    /* Can't use variadic js_path_join here - build manually */
    char buf[4096]; buf[0] = '\0';
    for (int i = 0; i < argc; i++) {
        char* s = js_to_cstr(argv[i]);
        size_t blen = strlen(buf);
        size_t slen = strlen(s);
        if (blen > 0 && buf[blen - 1] != '/' && slen > 0 && s[0] != '/')
            strncat(buf, "/", sizeof(buf) - blen - 1);
        strncat(buf, s, sizeof(buf) - strlen(buf) - 1);
        free(s);
    }
    return js_str(buf);
}
static inline JsValue _js_path_resolve_w(int argc, JsValue* argv) {
    return js_path_resolve(argc > 0 ? argv[0] : JS_UNDEFINED());
}
static inline JsValue _js_path_dirname_w(int argc, JsValue* argv) {
    return js_path_dirname(argc > 0 ? argv[0] : JS_UNDEFINED());
}
static inline JsValue _js_path_basename_w(int argc, JsValue* argv) {
    JsValue p   = argc > 0 ? argv[0] : JS_UNDEFINED();
    JsValue ext = argc > 1 ? argv[1] : JS_UNDEFINED();
    return js_path_basename(p, ext);
}
static inline JsValue _js_path_extname_w(int argc, JsValue* argv) {
    return js_path_extname(argc > 0 ? argv[0] : JS_UNDEFINED());
}

/* Build and return the path module object. */
static inline JsValue js_node_path(void) {
    JsValue m = js_obj_new();
    js_obj_set_fn(&m, "join",     _js_path_join_w);
    js_obj_set_fn(&m, "resolve",  _js_path_resolve_w);
    js_obj_set_fn(&m, "dirname",  _js_path_dirname_w);
    js_obj_set_fn(&m, "basename", _js_path_basename_w);
    js_obj_set_fn(&m, "extname",  _js_path_extname_w);
    return m;
}

/* ════════════════════════════════════════════════════════════════════════════
 * os module
 * ════════════════════════════════════════════════════════════════════════════ */

static inline JsValue js_os_platform(void) {
#if defined(__linux__)
    return js_str("linux");
#elif defined(__APPLE__)
    return js_str("darwin");
#elif defined(_WIN32) || defined(_WIN64)
    return js_str("win32");
#else
    return js_str("unknown");
#endif
}

static inline JsValue js_os_homedir(void) {
    const char* h = getenv("HOME");
#ifdef _WIN32
    if (!h || !h[0]) h = getenv("USERPROFILE");
#endif
    if (!h || !h[0]) h = getenv("TMPDIR");
    if (!h || !h[0]) h = "/tmp";
    return js_str(h);
}

static inline JsValue js_os_tmpdir(void) {
    const char* t = getenv("TMPDIR");
    if (!t || !t[0]) t = getenv("TMP");
    if (!t || !t[0]) t = "/tmp";
    return js_str(t);
}

static inline JsValue js_os_arch(void) {
#if defined(__x86_64__) || defined(_M_X64)
    return js_str("x64");
#elif defined(__aarch64__) || defined(_M_ARM64)
    return js_str("arm64");
#else
    return js_str("unknown");
#endif
}

/* Wrappers */
static inline JsValue _js_os_platform_w(int argc, JsValue* argv) { (void)argc; (void)argv; return js_os_platform(); }
static inline JsValue _js_os_arch_w(int argc, JsValue* argv)     { (void)argc; (void)argv; return js_os_arch(); }
static inline JsValue _js_os_homedir_w(int argc, JsValue* argv)  { (void)argc; (void)argv; return js_os_homedir(); }
static inline JsValue _js_os_tmpdir_w(int argc, JsValue* argv)   { (void)argc; (void)argv; return js_os_tmpdir(); }

/* Build and return the os module object. */
static inline JsValue js_node_os(void) {
    JsValue m = js_obj_new();
    js_obj_set_fn(&m, "platform", _js_os_platform_w);
    js_obj_set_fn(&m, "arch",     _js_os_arch_w);
    js_obj_set_fn(&m, "homedir",  _js_os_homedir_w);
    js_obj_set_fn(&m, "tmpdir",   _js_os_tmpdir_w);
    return m;
}

/* ════════════════════════════════════════════════════════════════════════════
 * url module (minimal parse)
 * ════════════════════════════════════════════════════════════════════════════ */

static inline JsValue js_url_parse(JsValue u) {
    char* s = js_to_cstr(u);
    const char* in = s ? s : "";
    const char* p = strstr(in, "://");
    const char* path = strchr(p ? (p + 3) : in, '/');
    if (!path) path = in + strlen(in);
    const char* q = strchr(path, '?');
    JsValue o = js_obj_new();
    js_obj_set(&o, "href", js_str(in));
    js_obj_set(&o, "path", js_str(path));
    if (q) {
        size_t plen = (size_t)(q - path);
        char* pb = (char*)malloc(plen + 1);
        memcpy(pb, path, plen);
        pb[plen] = '\0';
        js_obj_set(&o, "pathname", js_str(pb));
        js_obj_set(&o, "query", js_str(q + 1));
        free(pb);
    } else {
        js_obj_set(&o, "pathname", js_str(path));
        js_obj_set(&o, "query", js_str(""));
    }
    free(s);
    return o;
}
static inline JsValue _js_url_parse_w(int argc, JsValue* argv) {
    return js_url_parse(argc > 0 ? argv[0] : JS_UNDEFINED());
}
static inline JsValue js_node_url(void) {
    JsValue m = js_obj_new();
    js_obj_set_fn(&m, "parse", _js_url_parse_w);
    return m;
}

/* async_hooks minimal stub */
static inline JsValue js_node_async_hooks(void) {
    return js_obj_new();
}

/* querystring minimal shim */
static inline JsValue _js_qs_parse_w(int argc, JsValue* argv) {
    JsValue in = argc > 0 ? argv[0] : JS_UNDEFINED();
    char* s = js_to_cstr(in);
    JsValue out = js_obj_new();
    if (s && s[0]) {
        char* p = s;
        while (*p) {
            char* key = p;
            while (*p && *p != '=' && *p != '&') p++;
            char saved = *p;
            if (*p) *p++ = '\0';
            char* val = p;
            while (*p && *p != '&') p++;
            if (*p) *p++ = '\0';
            if (saved == '=') js_obj_set(&out, key, js_str(val));
            if (*p == '&') p++;
        }
    }
    free(s);
    return out;
}
static inline JsValue _js_qs_stringify_w(int argc, JsValue* argv) {
    (void)argc; (void)argv;
    return js_str("");
}
static inline JsValue js_node_querystring(void) {
    JsValue m = js_obj_new();
    js_obj_set_fn(&m, "parse", _js_qs_parse_w);
    js_obj_set_fn(&m, "stringify", _js_qs_stringify_w);
    return m;
}

/* zlib minimal shim */
static inline JsValue js_node_zlib(void) {
    return js_obj_new();
}

/* ════════════════════════════════════════════════════════════════════════════
 * process module
 * ════════════════════════════════════════════════════════════════════════════ */

/*
 * process.cwd() → current working directory
 */
static inline JsValue js_process_cwd(void) {
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf))) return js_str(buf);
    return js_str(".");
}

/*
 * process.exit(code) - exits the program.  Never returns.
 */
static inline JsValue js_process_exit(JsValue code) {
    exit((int)js_to_num(code));
    /* unreachable */
    return JS_UNDEFINED();
}

/*
 * process.argv - returns a JsObject with numeric string keys ("0", "1", …)
 * and a "length" property, matching the shape of the real Node.js process.argv.
 * Supports argv[i] access via computed member expression and .length lookup.
 * Capped at JS_OBJ_MAX_PROPS - 1 entries to leave room for "length".
 */
static inline JsValue js_process_get_argv(void) {
    JsValue arr = js_obj_new();
    int cap = JS_OBJ_MAX_PROPS - 1;
    int n = jsc_argc < cap ? jsc_argc : cap;
    for (int i = 0; i < n; i++) {
        char key[16];
        snprintf(key, sizeof(key), "%d", i);
        js_obj_set(&arr, key, js_str(jsc_argv[i]));
    }
    js_obj_set(&arr, "length", JS_NUM(jsc_argc));
    return arr;
}

/*
 * process.env.KEY → getenv(KEY), or JS_UNDEFINED if not set.
 */
static inline JsValue js_process_env_get(JsValue key) {
    char* k = js_to_cstr(key);
    const char* val = getenv(k);
    free(k);
    return val ? js_str(val) : JS_UNDEFINED();
}

/* Wrappers */
static inline JsValue _js_process_cwd_w(int argc, JsValue* argv) { (void)argc; (void)argv; return js_process_cwd(); }
static inline JsValue _js_process_exit_w(int argc, JsValue* argv) {
    return js_process_exit(argc > 0 ? argv[0] : JS_NUM(0));
}
static inline JsValue _js_process_env_get_w(int argc, JsValue* argv) {
    return js_process_env_get(argc > 0 ? argv[0] : JS_UNDEFINED());
}

/* Snapshot of getenv space for process.env boot checks (capacity-limited). */
static inline JsValue js_process_env_snapshot(void) {
    JsValue env = js_obj_new();
    if (!env.as.obj || !environ) return env;
    int cap = JS_OBJ_MAX_PROPS;
    int n = 0;
    for (char** ep = environ; *ep != NULL && n < cap; ++ep) {
        const char* kv = *ep;
        char key_buf[JS_OBJ_KEY_MAX];
        const char* eq = strchr(kv, '=');
        if (!eq || eq == kv) continue;
        size_t nk = (size_t)(eq - kv);
        if (nk >= sizeof(key_buf)) nk = sizeof(key_buf) - 1;
        memcpy(key_buf, kv, nk);
        key_buf[nk] = '\0';
        const char* val = eq + 1;
        JsValue vv = JS_STR(val);
        js_obj_set(&env, key_buf, vv);
        n++;
    }
    return env;
}

static inline JsValue _js_process_nextTick_w(int argc, JsValue* argv) {
    if (argc < 1) return JS_UNDEFINED();
    JsValue cb = argv[0];
    if (cb.tag != JS_FUNCTION || !cb.as.func) return JS_UNDEFINED();
    return js_call_argv(cb, 0, NULL);
}

/* Build and return the process module object.
 * argc/argv are stored globally via js_process_init(); this function reads
 * the globals so it can be called after main() has started. */
static inline JsValue js_node_process(void) {
    JsValue m = js_obj_new();
    js_obj_set_fn(&m, "cwd",       _js_process_cwd_w);
    js_obj_set_fn(&m, "exit",      _js_process_exit_w);
    js_obj_set_fn(&m, "nextTick",  _js_process_nextTick_w);
    js_obj_set(&m, "argv", js_process_get_argv());
    js_obj_set(&m, "env",  js_process_env_snapshot());
    js_obj_set(&m, "pid",  JS_NUM((double)getpid()));
    js_obj_set(&m, "platform", js_os_platform());
    js_obj_set(&m, "version", JS_STR("v0.0-blue"));
    JsValue versions = js_obj_new();
    js_obj_set(&versions, "node", JS_STR("0.0-blue"));
    js_obj_set(&m, "versions", versions);
    return m;
}

/* ════════════════════════════════════════════════════════════════════════════
 * util module (minimal for npm packages)
 * ════════════════════════════════════════════════════════════════════════════ */

static inline JsValue js_util_format(int argc, JsValue* argv) {
    if (argc <= 0) return js_str("");
    char* s = js_to_cstr(argv[0]);
    JsValue out = js_str(s ? s : "");
    free(s);
    return out;
}
static inline JsValue _js_util_format_w(int argc, JsValue* argv) {
    return js_util_format(argc, argv);
}
static inline JsValue _js_util_deprecate_w(int argc, JsValue* argv) {
    return argc > 0 ? argv[0] : JS_UNDEFINED();
}
static inline JsValue _js_util_inherits_w(int argc, JsValue* argv) {
    (void)argc; (void)argv;
    return JS_UNDEFINED();
}
static inline JsValue _js_util_inspect_w(int argc, JsValue* argv) {
    if (argc <= 0) return js_str("undefined");
    char* s = js_to_cstr(argv[0]);
    JsValue out = js_str(s ? s : "undefined");
    free(s);
    return out;
}
static inline JsValue js_node_util(void) {
    JsValue m = js_obj_new();
    js_obj_set_fn(&m, "format", _js_util_format_w);
    js_obj_set_fn(&m, "deprecate", _js_util_deprecate_w);
    js_obj_set_fn(&m, "inherits", _js_util_inherits_w);
    js_obj_set_fn(&m, "inspect", _js_util_inspect_w);
    return m;
}

/* ════════════════════════════════════════════════════════════════════════════
 * tty module (minimal)
 * ════════════════════════════════════════════════════════════════════════════ */

static inline JsValue _js_tty_isatty_w(int argc, JsValue* argv) {
    (void)argc; (void)argv;
    return JS_BOOL(0);
}
static inline JsValue js_node_tty(void) {
    JsValue m = js_obj_new();
    js_obj_set_fn(&m, "isatty", _js_tty_isatty_w);
    return m;
}

/* ════════════════════════════════════════════════════════════════════════════
 * js_node_require - the real implementation of require() for built-ins.
 * The emitter generates calls to this function for all bare requires
 * (require('fs'), require('path'), etc.).
 * ════════════════════════════════════════════════════════════════════════════ */

static inline JsValue js_node_require(JsValue name) {
    if (name.tag != JS_STRING || !name.as.string)
        return JS_UNDEFINED();
    const char* n = name.as.string;
    if (strcmp(n, "fs")      == 0) return js_node_fs();
    if (strcmp(n, "path")    == 0) return js_node_path();
    if (strcmp(n, "os")      == 0) return js_node_os();
    if (strcmp(n, "url")     == 0) return js_node_url();
    if (strcmp(n, "async_hooks") == 0) return js_node_async_hooks();
    if (strcmp(n, "querystring") == 0) return js_node_querystring();
    if (strcmp(n, "zlib") == 0) return js_node_zlib();
    if (strcmp(n, "util")    == 0) return js_node_util();
    if (strcmp(n, "tty")     == 0) return js_node_tty();
    if (strcmp(n, "process") == 0) return js_node_process();
    if (strcmp(n, "buffer") == 0) return js_node_buffer_module_bundle();
    if (strcmp(n, "http") == 0) return jsc_node_http_module();
    if (strcmp(n, "https") == 0) return jsc_node_https_module();
    if (strcmp(n, "net") == 0) return jsc_node_net_module();
    if (strcmp(n, "stream") == 0) return jsc_node_stream_module();
    if (strcmp(n, "events") == 0) return jsc_node_events_module();
    if (strcmp(n, "crypto") == 0) return jsc_node_crypto_module();
    /* electron stub - implemented in the next task */
    if (strcmp(n, "electron") == 0) return JS_UNDEFINED();
    /* Unknown builtins */
    return JS_UNDEFINED();
}

#endif /* JS_NODE_H */
