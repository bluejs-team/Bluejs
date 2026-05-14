#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "cli/support.hpp"

namespace fs = std::filesystem;

static void invokeCompiler(const std::string& binDirectory, const std::string& cxx,
                           const fs::path& tmpCc, const std::string& outputBinary,
                           const std::string& cppSource, bool forceWebviewExtra,
                           const fs::path& embedCppGenerated,
                           const std::string& extraCFlags) {
    fs::path    jsIncludeP = fs::path(binDirectory) / "src";
    std::string jsInclude =
        fs::weakly_canonical(jsIncludeP).generic_string();

    fs::path qjsVendorIncl = fs::weakly_canonical(
        fs::path(binDirectory) / "vendor" / "quickjs");
#ifdef _WIN32
    fs::path qjsObjsDir = firstExistingPath({
        fs::path(binDirectory) / "vendor" / "quickjs" / "obj-win",
        fs::path(binDirectory) / "vendor" / "quickjs" / "obj"
    });
#else
    fs::path qjsObjsDir = fs::path(binDirectory) / "vendor" / "quickjs" / "obj";
#endif
    if (!qjsObjsDir.empty())
        qjsObjsDir = fs::weakly_canonical(qjsObjsDir);

#ifdef _WIN32
    fs::path runtimeLibPath = firstExistingPath({
        fs::path(binDirectory) / "src" / "runtime" / "libblue_runtime_win.a",
        fs::path(binDirectory) / "src" / "runtime" / "libblue_runtime.a"
    });
    fs::path runtimeHybridLibPath = firstExistingPath({
        fs::path(binDirectory) / "src" / "runtime" / "libblue_runtime_hybrid_win.a",
        fs::path(binDirectory) / "src" / "runtime" / "libblue_runtime_hybrid.a"
    });
    fs::path runtimeWebviewLibPath = firstExistingPath({
        fs::path(binDirectory) / "src" / "runtime" / "libblue_runtime_webview_win.a",
        fs::path(binDirectory) / "src" / "runtime" / "libblue_runtime_webview.a"
    });
#else
    fs::path runtimeLibPath        = fs::path(binDirectory) / "src" / "runtime" / "libblue_runtime.a";
    fs::path runtimeHybridLibPath  = fs::path(binDirectory) / "src" / "runtime" / "libblue_runtime_hybrid.a";
    fs::path runtimeWebviewLibPath = fs::path(binDirectory) / "src" / "runtime" / "libblue_runtime_webview.a";
#endif
    bool haveRuntimeLib = fs::exists(runtimeLibPath);

    bool wantsQ =
        cppSource.find("#define JSC_RUNTIME_QUICKJS") != std::string::npos ||
        cppSource.find("jsc_quickjs_") != std::string::npos;
    bool wantsU =
        cppSource.find("#define JSC_RUNTIME_UV") != std::string::npos ||
        cppSource.find("jsc_uv_") != std::string::npos;
    bool wantsIo =
        cppSource.find("JSC_RUNTIME_NODE_IO") != std::string::npos;
    bool wantsCp =
        cppSource.find("JSC_QJS_CONTROL_PLANE") != std::string::npos ||
        cppSource.find("jsc_control_plane_run_embedded") != std::string::npos;
    bool wantsHybrid =
        cppSource.find("JSC_HYBRID") != std::string::npos ||
        cppSource.find("jsc_hybrid_boot_island") != std::string::npos;
    bool wantsW =
        forceWebviewExtra ||
        cppSource.find("#define JSC_RUNTIME_WEBVIEW") != std::string::npos;

#ifdef _WIN32
    bool haveUvLib = false;
    {
        const char* uvp = std::getenv("LIBUV_PATH");
        if (uvp && uvp[0]) haveUvLib = true;
    }
    if (!haveUvLib) {
        fs::path bundledUv = firstExistingPath({
            fs::path(binDirectory) / "vendor" / "libuv-win",
            fs::path(binDirectory) / "vendor" / "libuv"
        });
        if (fs::exists(bundledUv / "include" / "uv.h") &&
            (fs::exists(bundledUv / "lib" / "libuv.a") ||
             fs::exists(bundledUv / "lib" / "libuv_a.a")))
            haveUvLib = true;
    }
#else
    bool haveUvLib =
        (::system((std::string("pkg-config --exists libuv >") + devNull() + " 2>&1").c_str()) == 0);
#endif

#if defined(__linux__)
    bool haveGtk =
        (::system((std::string("pkg-config --exists gtk+-3.0 >") + devNull() + " 2>&1").c_str()) == 0);
    std::string wkPc =
        (::system((std::string("pkg-config --exists webkit2gtk-4.1 >") + devNull() + " 2>&1").c_str()) == 0)
            ? "webkit2gtk-4.1"
            : "webkit2gtk-4.0";
    bool haveWk =
        (::system(("pkg-config --exists " + wkPc + " >" + devNull() + " 2>&1").c_str()) == 0);
#else
    bool           haveGtk = true;
    bool           haveWk  = true;
    std::string    wkPc    = "";
    (void)haveGtk; (void)haveWk; (void)wkPc;
#endif

    std::string extraSrc;
    std::string extraObjs;

    if (haveRuntimeLib) {
        bool needsHybrid  = wantsU || wantsHybrid || wantsCp;
        bool haveHybridLib  = fs::exists(runtimeHybridLibPath);
        bool haveWebviewLib = fs::exists(runtimeWebviewLibPath);

        extraObjs += " -Wl,--start-group";
        if (wantsW && haveWebviewLib)
            extraObjs += " \"" + fs::weakly_canonical(runtimeWebviewLibPath).generic_string() + "\"";
        if (needsHybrid && haveHybridLib)
            extraObjs += " \"" + fs::weakly_canonical(runtimeHybridLibPath).generic_string() + "\"";
        extraObjs += " \"" + fs::weakly_canonical(runtimeLibPath).generic_string() + "\"";
        extraObjs += " -Wl,--end-group";

        {
            static const char* objs[] = {"quickjs.o",    "dtoa.o",       "libregexp.o",
                                         "libunicode.o", "cutils.o",     "quickjs-libc.o"};
            for (const char* fo : objs)
                extraObjs += " \"" + (qjsObjsDir / fo).generic_string() + "\"";
        }

        if (wantsW && !haveWebviewLib) {
#if defined(__linux__)
            if (!haveGtk || !haveWk)
                throw std::runtime_error(
                    "WebView builds need gtk+-3.0 + webkit2gtk-4.x dev packages.");
            extraSrc += " \"" + (jsIncludeP / "runtime" / "linux" / "jsc_webview_linux.cpp").generic_string() + "\"";
            extraSrc += " \"" + jsInclude + "/runtime/linux/jsc_blue_native_linux.cpp\"";
#elif defined(__APPLE__)
            extraSrc += " \"" + (jsIncludeP / "runtime" / "macos" / "jsc_webview_mac.mm").generic_string() + "\"";
#else
            extraSrc += " \"" + (jsIncludeP / "runtime" / "windows" / "jsc_webview_win.cpp").generic_string() + "\"";
            extraSrc += " \"" + jsInclude + "/runtime/windows/jsc_blue_native_win.cpp\"";
#endif
        }

        if (wantsW && !embedCppGenerated.empty() && fs::exists(embedCppGenerated))
            extraSrc += " \"" + fs::weakly_canonical(fs::path(embedCppGenerated)).generic_string() + "\"";
    } else {
        if (wantsQ) {
            extraSrc += " \"" + jsInclude + "/runtime/jsc_quickjs_fallback.cpp\"";
            static const char* objs[] = {"quickjs.o",    "dtoa.o",       "libregexp.o",
                                         "libunicode.o", "cutils.o",     "quickjs-libc.o"};
            for (const char* fo : objs)
                extraObjs += " \"" + (qjsObjsDir / fo).generic_string() + "\"";
        }
        if (wantsQ || wantsU)
            extraSrc += " \"" + jsInclude + "/runtime/jsc_event_loop.cpp\"";
        extraSrc += " \"" + jsInclude + "/runtime/jsc_node_io.cpp\"";
        if (wantsCp || wantsHybrid) {
            extraSrc += " \"" + jsInclude + "/runtime/jsc_qjs_node_bridge.cpp\"";
            extraSrc += " \"" + jsInclude + "/runtime/jsc_blue_qjs.cpp\"";
            extraSrc += " \"" + jsInclude + "/runtime/jsc_blue_plugin.cpp\"";
        }
        if (wantsHybrid)
            extraSrc += " \"" + jsInclude + "/runtime/jsc_hybrid_runtime.cpp\"";

        fs::path webviewTU = fs::weakly_canonical(
            jsIncludeP / "runtime" / "jsc_webview_stub.cpp");
        fs::path embedTU = fs::weakly_canonical(
            jsIncludeP / "runtime" / "jsc_embed_stub.cpp");

        if (wantsW) {
#if defined(__linux__)
            if (!haveGtk || !haveWk)
                throw std::runtime_error(
                    "WebView builds need gtk+-3.0 + webkit2gtk-4.x dev packages.");
            webviewTU =
                fs::weakly_canonical(jsIncludeP / "runtime" / "linux" / "jsc_webview_linux.cpp");
#elif defined(__APPLE__)
            webviewTU =
                fs::weakly_canonical(jsIncludeP / "runtime" / "macos" / "jsc_webview_mac.mm");
#else
            webviewTU =
                fs::weakly_canonical(jsIncludeP / "runtime" / "windows" / "jsc_webview_win.cpp");
#endif
            if (!embedCppGenerated.empty() && fs::exists(embedCppGenerated))
                embedTU = embedCppGenerated;

            extraSrc += " \"" + webviewTU.generic_string() + "\" \"" +
                        embedTU.generic_string() + "\"";
        } else {
            extraSrc += " \"" + webviewTU.generic_string() + "\"";
        }

        extraSrc += " \"" + jsInclude + "/runtime/jsc_blue_native_core.cpp\"";
        if (wantsW) {
#if defined(__linux__)
            extraSrc += " \"" + jsInclude + "/runtime/linux/jsc_blue_native_linux.cpp\"";
#elif defined(_WIN32)
            extraSrc += " \"" + jsInclude + "/runtime/windows/jsc_blue_native_win.cpp\"";
#else
            extraSrc += " \"" + jsInclude + "/runtime/jsc_blue_native_gui_stub.cpp\"";
#endif
        } else {
            extraSrc += " \"" + jsInclude + "/runtime/jsc_blue_native_gui_stub.cpp\"";
        }
    }

    std::string gtkCflags, gtkLibs, wkCflags, wkLibs;
#if defined(__linux__)
    if (wantsW && haveGtk && haveWk) {
        gtkCflags = readPkgCfgLine("pkg-config --cflags gtk+-3.0 2>/dev/null");
        gtkLibs   = readPkgCfgLine("pkg-config --libs gtk+-3.0 2>/dev/null");
        wkCflags  = readPkgCfgLine(("pkg-config --cflags " + wkPc + " 2>/dev/null").c_str());
        wkLibs    = readPkgCfgLine(("pkg-config --libs " + wkPc + " 2>/dev/null").c_str());
    }
#endif

    std::string uvCflags, uvLibs;
    if (wantsU && haveUvLib) {
#ifdef _WIN32
        {
            const char* uvp = std::getenv("LIBUV_PATH");
            std::string uvDir;
            if (uvp && uvp[0]) {
                uvDir = uvp;
            } else {
                fs::path bundled = firstExistingPath({
                    fs::path(binDirectory) / "vendor" / "libuv-win",
                    fs::path(binDirectory) / "vendor" / "libuv"
                });
                if (fs::exists(bundled / "include" / "uv.h"))
                    uvDir = bundled.generic_string();
            }
            if (!uvDir.empty()) {
                uvCflags = "-I\"" + uvDir + "/include\" -DUV_STATIC_BUILD=1";
                fs::path staticA = fs::path(uvDir) / "lib" / "libuv_a.a";
                fs::path staticB = fs::path(uvDir) / "lib" / "libuv.a";
                std::string uvlib = fs::exists(staticA) ? staticA.generic_string()
                                                        : staticB.generic_string();
                uvLibs = "\"" + uvlib + "\"";
            }
        }
#else
        uvCflags = readPkgCfgLine("pkg-config --cflags libuv 2>/dev/null");
#if defined(__linux__)
        {
            static const char* candidates[] = {
                "/usr/lib/x86_64-linux-gnu/libuv.a",
                "/usr/lib/aarch64-linux-gnu/libuv.a",
                "/usr/lib/arm-linux-gnueabihf/libuv.a",
                "/usr/lib/libuv.a",
                "/usr/local/lib/libuv.a",
                nullptr
            };
            for (int ci = 0; candidates[ci]; ++ci) {
                if (fs::exists(candidates[ci])) {
                    uvLibs = std::string("\"") + candidates[ci] + "\" -lpthread -ldl -lrt";
                    break;
                }
            }
        }
        if (uvLibs.empty())
#endif
        uvLibs = readPkgCfgLine("pkg-config --libs libuv 2>/dev/null");
#endif
    }

#ifdef _WIN32
    bool haveSsl = false;
    {
        const char* sslp = std::getenv("OPENSSL_PATH");
        if (sslp && sslp[0]) haveSsl = true;
    }
#else
    bool haveSsl =
        (::system((std::string("pkg-config --exists openssl >") + devNull() + " 2>&1").c_str()) == 0);
#endif
    std::string sslCflags, sslLibs;
    if (wantsIo && haveSsl) {
        sslCflags = readPkgCfgLine("pkg-config --cflags openssl 2>/dev/null");
        sslLibs   = readPkgCfgLine("pkg-config --libs openssl 2>/dev/null");
    }

#ifdef _WIN32
    fs::path webview2Sdk = wantsW ? findBundledWebView2Sdk(binDirectory) : fs::path();
#endif

    std::string compileCmd;
    compileCmd += shellQuoteIfNeeded(cxx) + " ";
#if defined(__APPLE__)
    if (wantsW)
        compileCmd += "-ObjC++ ";
#endif
    compileCmd +=
        "-std=c++17 -O2 -Wall -Wextra -I\"" + jsInclude + "\"";
    if (haveRuntimeLib)
        compileCmd += " -ffunction-sections -fdata-sections";

    if (!gtkCflags.empty())
        compileCmd += " " + gtkCflags;
    if (!wkCflags.empty())
        compileCmd += " " + wkCflags;
    if (wantsIo && haveSsl && !sslCflags.empty())
        compileCmd += " " + sslCflags;

    if (wantsQ)
        compileCmd += " -I\"" + qjsVendorIncl.generic_string() + "\"";

    compileCmd += " -o \"" + outputBinary + "\" \"" + tmpCc.generic_string() +
                   "\"" + extraSrc + extraObjs;

    if (wantsU && haveUvLib) {
        if (!uvCflags.empty())
            compileCmd += " " + uvCflags;
        compileCmd += " -DJSC_HAVE_UV=1";
    }
    if (wantsIo) {
        if (!haveUvLib)
            throw std::runtime_error(
                "blue: require('http'|'net'|...) needs libuv - install libuv "
                "development packages (pkg-config libuv).");
        compileCmd += " -DJSC_RUNTIME_NODE_IO=1";
    }
    if (wantsIo && haveSsl) {
        compileCmd += " -DJSC_HAVE_OPENSSL=1";
    }
    if (wantsCp || wantsHybrid)
        compileCmd += " -DJSC_QJS_CONTROL_PLANE=1";
    if (wantsHybrid)
        compileCmd += " -DJSC_HYBRID=1";
    if (wantsQ)
        compileCmd += " -DJSC_RUNTIME_QUICKJS=1";

#ifndef _WIN32
    compileCmd += " -lm -pthread";
#endif
#if defined(__linux__)
    compileCmd += " -ldl";
    if (haveRuntimeLib)
        compileCmd += " -Wl,--gc-sections";
#endif

#ifdef __APPLE__
    if (wantsW)
        compileCmd += " -framework WebKit -framework Cocoa ";
#endif

#ifdef _WIN32
    {
        bool isMSVC = (cxx.find("cl") != std::string::npos &&
                       cxx.find("clang") == std::string::npos &&
                       cxx.find("g++") == std::string::npos);

        if (wantsW) {
            if (isMSVC)
                compileCmd += " user32.lib gdi32.lib ole32.lib shell32.lib"
                              " comdlg32.lib comctl32.lib oleaut32.lib uuid.lib";
            else
                compileCmd += " -luser32 -lgdi32 -lole32 -lshell32"
                              " -lcomdlg32 -lcomctl32 -loleaut32 -luuid";
        }

        if (!webview2Sdk.empty() && wantsW) {
            std::string sdk = webview2Sdk.generic_string();
            compileCmd += " -DBLUE_HAVE_WEBVIEW2=1";
            compileCmd += " -I\"" + sdk + "/build/native/include\"";
            if (isMSVC)
                compileCmd += " \"" + sdk + "/build/native/x64/WebView2Loader.dll.lib\"";
            else
                compileCmd += " -L\"" + sdk + "/build/native/x64\" -lWebView2Loader.dll";
        }

        if (!isMSVC)
            compileCmd += " -static-libgcc -static-libstdc++ -Wl,-Bstatic -lpthread -Wl,-Bdynamic";
    }
#endif

    if (!gtkLibs.empty())
        compileCmd += " " + gtkLibs;
    if (!wkLibs.empty())
        compileCmd += " " + wkLibs;

    compileCmd += extraCFlags.empty()
                       ? ""
                       : (" " + flattenFlagsForShell(extraCFlags));

    if (wantsU && haveUvLib && !uvLibs.empty())
        compileCmd += " " + uvLibs;
    if (wantsIo && haveSsl && !sslLibs.empty())
        compileCmd += " " + sslLibs;

#ifdef _WIN32
    {
        bool isMSVC = (cxx.find("cl") != std::string::npos &&
                       cxx.find("clang") == std::string::npos &&
                       cxx.find("g++") == std::string::npos);
        if ((wantsU || wantsHybrid || wantsCp) && haveUvLib) {
            if (isMSVC)
                compileCmd += " ws2_32.lib iphlpapi.lib userenv.lib dbghelp.lib psapi.lib bcrypt.lib ole32.lib";
            else
                compileCmd += " -lws2_32 -liphlpapi -luserenv -ldbghelp -lpsapi -lbcrypt -lole32";
        }
    }
#endif

    compileCmd += " 2>&1";

    int rc = std::system(compileCmd.c_str());
    if (shellCommandFailed(rc))
        throw std::runtime_error(
            "blue: C++ compiler failed.\n(hint run with --print-c to inspect)");

#ifdef _WIN32
    if (wantsW && !webview2Sdk.empty()) {
        fs::path loader = webview2Sdk / "build" / "native" / "x64" / "WebView2Loader.dll";
        if (!fs::exists(loader))
            loader = webview2Sdk / "runtimes" / "win-x64" / "native" / "WebView2Loader.dll";
        if (fs::exists(loader)) {
            std::error_code ec;
            fs::path dest = fs::path(outputBinary).parent_path() / "WebView2Loader.dll";
            if (!fs::exists(dest, ec)) {
                ec.clear();
                fs::copy_file(loader, dest, fs::copy_options::overwrite_existing, ec);
            } else {
                ec.clear();
            }
            if (ec)
                std::cerr << "blue: warning: could not copy WebView2Loader.dll: "
                          << ec.message() << "\n";
        }
    }
#endif

    maybeSignExecutable(outputBinary);
}
