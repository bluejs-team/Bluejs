#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifndef popen
#define popen  _popen
#endif
#ifndef pclose
#define pclose _pclose
#endif
#ifndef WIFEXITED
#define WIFEXITED(s)   (true)
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(s) (s)
#endif
#endif

#include <nlohmann/json.hpp>

#include "quickjs.h"

namespace fs = std::filesystem;

static std::string readFileToString(const std::string& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("blue: cannot open file: " + path);
    std::ostringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

static bool packageLooksReactProject(const fs::path& packageJsonPath) {
    if (!fs::exists(packageJsonPath))
        return false;
    nlohmann::json pj;
    {
        std::ifstream in(packageJsonPath);
        if (!in)
            return false;
        in >> pj;
    }
    auto hasDep = [&](const char* section, const char* name) -> bool {
        return pj.contains(section) && pj[section].is_object() &&
               pj[section].contains(name);
    };
    bool hasReact = hasDep("dependencies", "react") ||
                    hasDep("devDependencies", "react") ||
                    hasDep("peerDependencies", "react");
    bool hasReactDom = hasDep("dependencies", "react-dom") ||
                       hasDep("devDependencies", "react-dom") ||
                       hasDep("peerDependencies", "react-dom");
    return hasReact || hasReactDom;
}

static void maybeSignExecutable(const fs::path& bin) {
#ifdef __APPLE__
    const char* ident = std::getenv("CODESIGN_IDENTITY");
    if (ident && *ident) {
        std::string cmd = std::string("codesign --force --sign \"") + ident +
                          "\" \"" + bin.string() + "\" 2>&1";
        int rc = std::system(cmd.c_str());
        std::cerr << "blue: codesign exited " << rc << "\n";
    }
#elif defined(_WIN32)
    const char* sg = std::getenv("SIGNTOOL_PATH");
    if (sg && *sg && std::getenv("SIGN_CERT_THUMBPRINT")) {
        std::ostringstream o;
        o << "\"" << sg << "\" sign /sha1 "
          << std::getenv("SIGN_CERT_THUMBPRINT")
          << " /tr http://timestamp.digicert.com /td sha256 /fd "
             "sha256 \""
          << bin.string().c_str() << "\"";
        std::string sc = "\"" + o.str() + "\"";
        std::system(sc.c_str());
    }
#endif
    (void)bin;
}

struct QjsGuard {
    JSRuntime* rt;
    JSContext* ctx;
    QjsGuard() : rt(JS_NewRuntime()), ctx(nullptr) {
        if (rt)
            ctx = JS_NewContext(rt);
    }
    ~QjsGuard() {
        if (ctx)
            JS_FreeContext(ctx);
        if (rt)
            JS_FreeRuntime(rt);
    }
    QjsGuard(const QjsGuard&)            = delete;
    QjsGuard& operator=(const QjsGuard&) = delete;
};

static void qjsThrowIfException(JSContext* ctx, const std::string& what) {
    JSValue       ex = JS_GetException(ctx);
    const char* msg = JS_ToCString(ctx, ex);
    std::string err = what + ": ";
    err += msg ? msg : "exception";
    if (msg)
        JS_FreeCString(ctx, msg);
    JS_FreeValue(ctx, ex);
    throw std::runtime_error(err);
}

static void qjsEvalScript(JSContext* ctx, const std::string& code,
                          const char* filename) {
    JSValue v = JS_Eval(ctx, code.data(), code.size(), filename,
                        JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        JS_FreeValue(ctx, v);
        qjsThrowIfException(ctx, std::string("blue: ") + filename);
    }
    JS_FreeValue(ctx, v);
}

static const char* devNull() {
#ifdef _WIN32
    return "NUL";
#else
    return "/dev/null";
#endif
}

static std::string readPipe(const char* cmd) {
    FILE* pf = ::popen(cmd, "r");
    if (!pf) {
        std::cerr << "blue: warning: popen failed for: " << cmd << "\n";
        return "";
    }
    std::ostringstream acc;
    char               buf[2048];
    size_t nr;
    while ((nr = fread(buf, 1, sizeof(buf), pf)) > 0)
        acc.write(buf, nr);
    pclose(pf);
    std::string s = acc.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

/* pkg-config --cflags/-libs emit newlines; shell driver commands need spaces. */
static std::string flattenFlagsForShell(std::string s) {
    for (char& c : s) {
        if (c == '\n' || c == '\r' || c == '\t')
            c = ' ';
    }
    std::string out;
    out.reserve(s.size());
    bool sp = false;
    for (unsigned char uc : s) {
        char c = (char)uc;
        if (c == ' ') {
            if (!sp) {
                out += ' ';
                sp = true;
            }
        } else {
            out += c;
            sp = false;
        }
    }
    while (!out.empty() && out.front() == ' ')
        out.erase(out.begin());
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

#ifdef _WIN32
static fs::path firstExistingPath(std::initializer_list<fs::path> paths) {
    for (const auto& p : paths) {
        if (fs::exists(p))
            return p;
    }
    return paths.size() ? *paths.begin() : fs::path();
}
#endif

static std::string shellQuoteIfNeeded(const std::string& s) {
    if (s.find_first_of(" \t\"") == std::string::npos)
        return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"')
            out += "\\\"";
        else
            out += c;
    }
    out += "\"";
    return out;
}

#ifdef _WIN32
static fs::path findBundledWebView2Sdk(const std::string& binDirectory) {
    const char* env = std::getenv("WEBVIEW2_SDK_PATH");
    if (env && env[0]) {
        fs::path p(env);
        if (fs::exists(p / "build" / "native" / "include" / "WebView2.h"))
            return p;
    }

    for (const fs::path& p : {
             fs::path(binDirectory) / "vendor" / "webview2",
             fs::path(binDirectory) / "Microsoft.Web.WebView2"
         }) {
        if (fs::exists(p / "build" / "native" / "include" / "WebView2.h"))
            return p;
    }

    return fs::path();
}
#endif

static bool shellCommandFailed(int st) {
#ifdef _WIN32
    return st != 0;
#else
    if (st == -1)
        return true;
    if (!WIFEXITED(st))
        return true;
    return WEXITSTATUS(st) != 0;
#endif
}

static std::string readPkgCfgLine(const char* cmd) {
    return flattenFlagsForShell(readPipe(cmd));
}

static std::vector<uint8_t> readFileToBytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + p.generic_string());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

static std::string defaultCxx() {
    std::string cxx =
#ifdef _WIN32
        "g++";
#else
        "c++";
#endif
    char* ec = getenv("CXX");
    if (ec && std::strlen(ec))
        cxx = ec;
    return cxx;
}

static void bootstrapQjs(JSContext* ctx, const std::string& binDirectory) {
    auto readWithFallback = [&](const std::string& name) -> std::string {
        for (const fs::path& base : {
                 fs::current_path(),
                 fs::path(binDirectory),
                 fs::current_path() / "vendor" / "js",
                 fs::path(binDirectory) / "vendor" / "js",
             }) {
            try {
                return readFileToString((base / name).generic_string());
            } catch (...) {}
        }
        throw std::runtime_error(
            name + " not found in CWD, vendor/js, or " + binDirectory +
            " - run make deps.");
    };
    std::string esprimaSrc = readWithFallback("esprima.js");
    qjsEvalScript(ctx, esprimaSrc, "esprima.js");
    qjsEvalScript(ctx,
                  "if (typeof globalThis==='undefined'){var "
                  "globalThis=this;}if(typeof console==='undefined'){var "
                  "console={log:function(){},error:function(){},warn:"
                  "function(){},info:"
                  "function(){},debug:function(){}};} ",
                  "<shim>");
    std::string babelSrc = readWithFallback("babel.min.js");
    qjsEvalScript(ctx, babelSrc, "babel.min.js");
}
