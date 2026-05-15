// main.cpp - Blue: JS-to-C++ compiler entry point
//
//   blue -compile <input.js> [-o out] [-cflags "extra"]
//   blue -init <dir>
//   blue -build <dir> [-o app] [--no-inline-html]

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* MSVC/MinGW: alias POSIX names */
#define popen  _popen
#define pclose _pclose
/* getpid() → GetCurrentProcessId() */
inline int getpid() { return static_cast<int>(GetCurrentProcessId()); }
/* pclose() on Windows returns the exit code directly, not POSIX-encoded */
#define WIFEXITED(s)   (true)
#define WEXITSTATUS(s) (s)
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ast_node.hpp"
#include "cli/args.hpp"
#include "cli/compiler_driver.hpp"
#include "cli/generated_sources.hpp"
#include "cli/paths.hpp"
#include "cli/project_scaffold.hpp"
#include "cli/support.hpp"
#include "emitter.hpp"
#include "jsc_bundle.hpp"
#include "jsc_embed_generator.hpp"
#include "jsc_npm_bundle.hpp"
#include "module_resolver.hpp"
#include "quickjs.h"

namespace fs = std::filesystem;

#ifndef BLUE_VERSION
#define BLUE_VERSION "dev"
#endif

static void printUsage(const char* prog) {
    const char* name = prog;
    if (const char* slash = strrchr(prog, '/')) name = slash + 1;
    if (const char* slash = strrchr(name, '\\')) name = slash + 1;
    const char* display = name;
    std::cerr << "Blue " << BLUE_VERSION << " - JavaScript-to-native compiler\n\n"
              << "Usage:\n"
              << "  " << display << " -compile <input.js> [-o out] [-cflags \"extra\"]\n"
              << "  " << display << " -init [<dir>] [--build]\n"
              << "  " << display << " -build [<dir>] [-o app] [--no-inline-html] [--time]\n"
              << "  " << display << " run [<dir>] [--time] [-- args...]\n"
              << "  " << display << " --print-c <input.js>\n"
              << "  " << display << " --version\n"
              << "  " << display << " --help\n\n"
              << "Tip: run with --print-c to inspect the generated C++ for any input.\n"
              << "Tip: unsupported JS features? Move them to src/island.js (full JS).\n";
}

int main(int argc, char** argv) {
    CliOptions opts;
    CliParseResult parseResult = parseArgs(argc, argv, opts, printUsage);
    if (parseResult == CliParseResult::ExitSuccess)
        return EXIT_SUCCESS;
    if (parseResult == CliParseResult::ExitFailure)
        return EXIT_FAILURE;

    if (opts.doInit) {
        try {
            cmdInit(opts.projDirInit);
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    std::string binDirectory = binDir(argv[0]);

    if (opts.doBuild) {
        auto t0 = std::chrono::steady_clock::now();
        try {
            fs::path proj = fs::absolute(opts.projDirBuild);
            if (!fs::exists(proj) || !fs::is_directory(proj))
                throw std::runtime_error("blue: project directory not found: " +
                                         proj.generic_string());
            fs::path bd   = proj / ".blue-build";
            fs::create_directories(bd);

            if (opts.initThenBuild)
                cmdInit(proj);

            fs::path cfgPath = proj / "blue.config.json";
            nlohmann::json cf = nlohmann::json::object();

            if (fs::exists(cfgPath) && fs::file_size(cfgPath) > 0) {
                std::ifstream jc(cfgPath);
                try {
                    jc >> cf;
                    if (!cf.is_object())
                        cf = nlohmann::json::object();
                } catch (...) {
                    cf = nlohmann::json::object();
                }
            }

            std::string pubName = cf.value("publicDir", std::string("public"));
            std::string entry   = cf.value("entry", std::string("src/main.js"));
            bool bundleDeps =
                cf.value("bundleDependencies", false);
            std::string bundlePlat =
                cf.value("bundlePlatform", std::string("neutral"));
            bool controlPlane = cf.value("controlPlane", false);
            bool hybrid       = cf.value("hybrid", fs::exists(proj / "src" / "island.js"));
            std::string quickjsIsland =
                cf.value("quickjsIsland", hybrid ? std::string("src/island.js") : std::string());

            // If entry was default src/main.js but that doesn't exist, try main.js
            if (!cf.contains("entry") && !fs::exists(proj / entry) && fs::exists(proj / "main.js")) {
                entry = "main.js";
            }


            if (controlPlane && hybrid)
                throw std::runtime_error(
                    "blue: config: \"controlPlane\" and \"hybrid\" "
                    "cannot both be true.");
            if (hybrid && quickjsIsland.empty())
                throw std::runtime_error(
                    "blue: hybrid builds require \"quickjsIsland\" "
                    "(path to island entry .js) in blue.config.json.");

            fs::path publicRoot = proj / pubName;
            fs::path entryJs    = proj / entry;
            if (!fs::exists(entryJs)) {
                throw std::runtime_error("blue: entry file not found: " +
                                         entryJs.generic_string());
            }
            fs::create_directories(publicRoot);

            // React project auto-frontend build:
            // If package.json indicates React and a frontend entry exists,
            // bundle it to public/app.bundle.js using esbuild.
            fs::path pkgJson = proj / "package.json";
            if (packageLooksReactProject(pkgJson)) {
                std::vector<fs::path> candidateEntries = {
                    proj / "src" / "frontend.jsx",
                    proj / "src" / "frontend.tsx",
                    proj / "src" / "frontend.js",
                    proj / "src" / "frontend.ts",
                };
                fs::path frontendEntry;
                for (const auto& c : candidateEntries) {
                    if (fs::exists(c)) {
                        frontendEntry = c;
                        break;
                    }
                }
                if (!frontendEntry.empty()) {
                    fs::path frontendBundle = publicRoot / "app.bundle.js";
                    jsc_npm_bundle::run(
                        binDirectory,
                        fs::absolute(frontendEntry).generic_string(),
                        proj.generic_string(),
                        fs::absolute(frontendBundle).generic_string(),
                        "browser");
                    std::cerr << "blue: built React frontend bundle "
                              << frontendBundle << "\n";
                }
            }

            std::vector<jsc_embed_gen::EmbedFile> assets =
                jsc_embed_gen::listPublicRecursive(publicRoot);

            if (!assets.empty())
                jsc_embed_gen::emitCpp(assets, bd / "jsc_assets.cpp");

            /* Bundle HTML referenced by JS: read main.js, replace sentinel */
            std::string mainTpl = readFileToString(entryJs.generic_string());

            fs::path    idxHtmlPath = publicRoot / "index.html";
            std::string bundledHtml =
                fs::exists(idxHtmlPath) && !opts.noInlineHtml
                    ? jsc_bundle::inlineHtmlDeps(fs::canonical(idxHtmlPath),
                                                 fs::canonical(publicRoot))
                    : std::string("");

            bool replacedAny = false;
            if (!bundledHtml.empty()) {
                std::string esc = "\"";
                for (char c : bundledHtml) {
                    if (c == '\\' || c == '\"') {
                        esc += '\\';
                        esc += c;
                    } else if (c == '\n')
                        esc += "\\n";
                    else if (c == '\r')
                        esc += "\\r";
                    else if (c == '\t')
                        esc += "\\t";
                    else
                        esc += c;
                }
                esc += '\"';
                for (const std::string& ph :
                     {std::string("__BLUE_BUNDLE_HTML__")}) {
                    size_t pos = mainTpl.find(ph);
                    while (pos != std::string::npos) {
                        mainTpl.replace(pos, ph.size(), esc);
                        pos = mainTpl.find(ph, pos + esc.size());
                        replacedAny = true;
                    }
                }
                if (!replacedAny)
                    throw std::runtime_error(
                        "src/main.js must contain placeholder __BLUE_BUNDLE_HTML__");
            }

            fs::path resolveJs = entryJs;
            if (replacedAny) {
                fs::path tmpMain = bd / "main.entry.js";
                {
                    std::ofstream wf(tmpMain);
                    wf << mainTpl;
                }
                resolveJs = tmpMain;
            }

            fs::path bundledMain;
            if (bundleDeps) {
                bundledMain = bd / "main.bundled.js";
                jsc_npm_bundle::run(binDirectory,
                                    fs::absolute(resolveJs).generic_string(),
                                    proj.generic_string(),
                                    fs::absolute(bundledMain).generic_string(),
                                    bundlePlat);
                resolveJs = bundledMain;
            }

            std::string cpp;
            if (controlPlane) {
                cpp = emitControlPlaneCpp(readFileToBytes(resolveJs));
            } else if (hybrid) {
                fs::path islandEntryPath = fs::weakly_canonical(proj / quickjsIsland);
                if (!fs::exists(islandEntryPath))
                    throw std::runtime_error(
                        "blue: hybrid: quickjsIsland file not found: " +
                        islandEntryPath.generic_string());
                fs::path islandBundled = bd / "island.bundled.js";
                jsc_npm_bundle::run(binDirectory,
                                    fs::absolute(islandEntryPath).generic_string(),
                                    proj.generic_string(),
                                    fs::absolute(islandBundled).generic_string(),
                                    bundlePlat);

                QjsGuard qjs;
                if (!qjs.rt || !qjs.ctx) {
                    std::cerr << "blue: could not allocate QuickJS\n";
                    return EXIT_FAILURE;
                }
                bootstrapQjs(qjs.ctx, binDirectory);

                std::vector<Module> mods =
                    ModuleResolver::resolve(resolveJs.generic_string(), qjs.ctx);
                ModuleResolver::validateHybridAotGraph(mods);

                CEmitter emitter;
                cpp = emitHybridIslandBlob(readFileToBytes(islandBundled)) +
                      emitter.emitModules(mods, true);
            } else {
                QjsGuard qjs;
                if (!qjs.rt || !qjs.ctx) {
                    std::cerr << "blue: could not allocate QuickJS\n";
                    return EXIT_FAILURE;
                }
                bootstrapQjs(qjs.ctx, binDirectory);

                std::vector<Module> mods =
                    ModuleResolver::resolve(resolveJs.generic_string(), qjs.ctx);

                CEmitter emitter;
                cpp = emitter.emitModules(mods);
            }

            fs::path outBin =
                opts.outputBinary.empty()
                    ? fs::current_path() / fs::path(proj.filename().string())
                    : fs::absolute(opts.outputBinary);
#ifdef _WIN32
            if (outBin.extension() != ".exe")
                outBin = outBin.replace_extension(".exe");
#endif

            fs::path tmpCc = fs::temp_directory_path() /
                ("blue_build_" + std::to_string(::getpid()) + "_" +
                 std::to_string(std::hash<std::string>{}(outBin.generic_string())) + ".cc");
            {
                std::ofstream oo(tmpCc);
                oo << cpp;
            }

            std::string cxx = defaultCxx();

            fs::path embed =
                fs::exists(bd / "jsc_assets.cpp") ? bd / "jsc_assets.cpp"
                                                     : fs::path();

            if (!bundledHtml.empty() && embed.empty() && !controlPlane && !hybrid)
                throw std::runtime_error(
                    "expected embedded assets after bundling.");

            invokeCompiler(binDirectory, cxx,
                           fs::weakly_canonical(tmpCc),
                           outBin.generic_string(), cpp,
                           false,
                           embed.generic_string(),
                           opts.extraCFlags);
            fs::remove(tmpCc);
            auto t1 = std::chrono::steady_clock::now();

            std::cerr << "blue: built " << outBin << "\n";

            if (opts.doBenchmark) {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                std::cerr << "blue benchmark (build): " << ms << "ms\n";
            }

            if (opts.doRun) {
                std::string runCmd = outBin.string();
                for (const auto& arg : opts.appArgs) {
                    runCmd += " \"";
                    runCmd += arg;
                    runCmd += "\"";
                }
                runCmd += " 2>&1";
                std::cerr << "blue: running " << outBin << " ...\n";
                auto tr0 = std::chrono::steady_clock::now();
                FILE* pipe = popen(runCmd.c_str(), "r");
                if (!pipe) return EXIT_FAILURE;
                char buf[4096];
                bool measuredStart = false;
                while (fgets(buf, sizeof(buf), pipe)) {
                    if (!measuredStart && opts.doBenchmark) {
                        auto tr1 = std::chrono::steady_clock::now();
                        auto ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(tr1 - tr0).count();
                        std::cerr << "blue benchmark (start): " << ms2 << "ms\n";
                        measuredStart = true;
                    }
                    std::cout << buf << std::flush;
                }
                int status = pclose(pipe);
                return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
            }
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    if (opts.inputPath.empty()) {
        std::cerr << "blue: missing input.\n";
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    if (!opts.doCompile)
        opts.printC = true;

    if (opts.doCompile && opts.outputBinary.empty()) {
        fs::path inp(opts.inputPath);
        std::string stem = inp.stem().generic_string();
        if (stem == "main" || stem == "index") {
            fs::path parent = inp.parent_path().filename();
            if (!parent.empty() && parent != "." && parent != "..")
                stem = parent.generic_string();
        }
        opts.outputBinary = stem;
    }
#ifdef _WIN32
    if (opts.doCompile) {
        fs::path op(opts.outputBinary);
        if (op.extension() != ".exe")
            opts.outputBinary = op.replace_extension(".exe").generic_string();
    }
#endif

    QjsGuard qjs;
    if (!qjs.rt || !qjs.ctx) {
        std::cerr << "blue: QuickJS unavailable\n";
        return EXIT_FAILURE;
    }

    try {
        bootstrapQjs(qjs.ctx, binDirectory);

        std::string modEntry = fs::absolute(opts.inputPath).generic_string();
        std::vector<Module> mods = ModuleResolver::resolve(modEntry, qjs.ctx);

        CEmitter emitter;
        std::string cpp = emitter.emitModules(mods);

        if (opts.printC)
            std::cout << cpp;

        if (opts.doCompile) {
            fs::path tmpCc = fs::temp_directory_path() /
                ("blue_compile_" + std::to_string(::getpid()) + "_" +
                 std::to_string(std::hash<std::string>{}(opts.outputBinary)) + ".cc");
            {
                std::ofstream oo(tmpCc);
                oo << cpp;
            }

            std::string cxx = defaultCxx();

            auto tc0 = std::chrono::steady_clock::now();
            invokeCompiler(binDirectory, cxx,
                           fs::weakly_canonical(tmpCc),
                           opts.outputBinary,
                           cpp,
                           false,
                           fs::path(),
                           opts.extraCFlags);
            fs::remove(tmpCc);
            std::cerr << "blue: compiled -> " << opts.outputBinary << "\n";
            if (opts.doBenchmark) {
                auto tc1 = std::chrono::steady_clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tc1 - tc0).count();
                std::cerr << "blue benchmark (compile): " << ms << "ms\n";
            }
        }

    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        std::string msg = ex.what();
        bool isParseOrFeature =
            msg.find("parse error") != std::string::npos ||
            msg.find("Unexpected") != std::string::npos ||
            msg.find("Unimplemented") != std::string::npos ||
            msg.find("SyntaxError") != std::string::npos;
        if (isParseOrFeature) {
            std::cerr << "\nTips:\n"
                      << "  • Run with --print-c to inspect the generated C++.\n"
                      << "  • Move unsupported code to src/island.js for full JS support.\n"
                      << "  • Set BLUE_STRICT_UNSUPPORTED=1 to turn warnings into errors.\n";
        }
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
