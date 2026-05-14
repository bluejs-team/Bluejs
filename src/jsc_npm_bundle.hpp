#pragma once
/*
 * Run tools/jsc-npm-bundle/bundle.mjs (esbuild) before ModuleResolver when
 * bundleDependencies / -bundle-npm is enabled.
 */

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace jsc_npm_bundle {

inline bool shellCommandFailed(int st) {
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

inline std::string shSingleQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += '\'';
    return out;
}

inline void run(const std::string& binDirectory, const std::string& entryAbs,
                const std::string& projectRootAbs, const std::string& outAbs,
                const std::string& platform) {
    fs::path scriptRel =
        fs::path(binDirectory) / "tools" / "jsc-npm-bundle" / "bundle.mjs";
    if (!fs::exists(scriptRel)) {
        throw std::runtime_error(
            "blue: missing npm bundle script at " +
            fs::absolute(scriptRel).generic_string() +
            "\n  Expected tools/jsc-npm-bundle beside the compiler binary.");
    }
    const char* nodeExe = std::getenv("BLUE_NODE");
    std::string node = (nodeExe && nodeExe[0]) ? std::string(nodeExe) : "";

    if (node.empty()) {
#ifdef _WIN32
        // Check PATH first via where.exe
        if (!shellCommandFailed(std::system("where node >nul 2>&1"))) {
            node = "node";
        } else {
            // Common install locations when PATH hasn't refreshed yet
            static const char* candidates[] = {
                "C:\\Program Files\\nodejs\\node.exe",
                "C:\\Program Files (x86)\\nodejs\\node.exe",
                nullptr
            };
            const char* lad = std::getenv("LOCALAPPDATA");
            std::string ladNode = lad ? std::string(lad) + "\\Programs\\nodejs\\node.exe" : "";
            for (int i = 0; candidates[i]; ++i) {
                if (fs::exists(candidates[i])) { node = candidates[i]; break; }
            }
            if (node.empty() && !ladNode.empty() && fs::exists(ladNode))
                node = ladNode;
        }
#else
        node = "node";
        if (shellCommandFailed(std::system("node --version >/dev/null 2>&1"))) {
            node = "";
        }
#endif
    }

    fs::path toolsDir = fs::path(binDirectory) / "tools" / "jsc-npm-bundle";
    fs::path marker = toolsDir / "node_modules" / "esbuild" / "package.json";
    if (!fs::exists(marker)) {
#ifdef _WIN32
        // On Windows, the node_modules directory is inside Program Files and requires
        // admin rights to write. Auto-install would fail silently for regular users.
        // The installer handles this with admin privileges - tell the user to reinstall.
        std::string msg = "blue: esbuild not installed.\n";
        if (node.empty()) {
            msg += "  Install Node.js from https://nodejs.org, then reinstall Blue\n"
                   "  from https://bluejs.dev to set up esbuild automatically.\n";
        } else {
            msg += "  Run the Blue installer again (it installs esbuild with admin rights),\n"
                   "  or run this once in an admin terminal:\n"
                   "    cd \"" + fs::absolute(toolsDir).generic_string() + "\"\n"
                   "    npm install\n";
        }
        msg += "  Then retry.";
        throw std::runtime_error(msg);
#else
        if (!node.empty()) {
            std::string npmCheck = "command -v npm >/dev/null 2>&1";
            if (std::system(npmCheck.c_str()) == 0) {
                std::cerr << "blue: esbuild missing, attempting auto-install via npm...\n";
                std::string toolsDirAbs = fs::absolute(toolsDir).generic_string();
                std::string icmd = "cd " + shSingleQuote(toolsDirAbs) + " && npm install";
                int rc = std::system(icmd.c_str());
                if (rc != 0)
                    std::cerr << "blue: auto-install failed (exit code " << rc << ").\n";
            }
        }
        if (!fs::exists(marker)) {
            throw std::runtime_error(
                "blue: esbuild not installed.\n"
                "  From the compiler repo root run:  make tools-deps\n"
                "  Then retry.");
        }
#endif
    }

    if (node.empty()) {
#ifdef _WIN32
        throw std::runtime_error(
            "blue: Node.js not found.\n"
            "  Install from https://nodejs.org then open a new terminal.\n"
            "  Or set BLUE_NODE=C:\\path\\to\\node.exe and retry.");
#else
        throw std::runtime_error(
            "blue: 'node' failed (install Node.js or set BLUE_NODE to the node binary). "
            "Required for npm dependency bundling.");
#endif
    }


    fs::path script = fs::absolute(scriptRel);
    std::ostringstream cmd;
#ifdef _WIN32
    // Use generic_string() (forward slashes) - node.js handles them fine on Windows.
    // Wrap each argument in double quotes.
    cmd << "\"" << node << "\""
        << " \"" << script.generic_string() << "\""
        << " \"" << entryAbs << "\""
        << " \"" << projectRootAbs << "\""
        << " \"" << outAbs << "\""
        << " \"" << platform << "\"";
    // std::system() on Windows calls: cmd.exe /C <cmd>
    // When <cmd> starts with a double-quote, cmd.exe strips the outermost pair of
    // quotes, mangling all inner quotes. Wrapping in an extra outer pair fixes this:
    //   cmd.exe /C ""node" "script" "entry" "root" "out" "platform""
    //   → strips outer pair → "node" "script" "entry" "root" "out" "platform"  ✓
    std::string finalCmd = "\"" + cmd.str() + "\"";
    int rc = std::system(finalCmd.c_str());
#else
    cmd << node << " " << shSingleQuote(script.generic_string()) << " "
        << shSingleQuote(entryAbs) << " " << shSingleQuote(projectRootAbs) << " "
        << shSingleQuote(outAbs) << " " << shSingleQuote(platform);
    int rc = std::system(cmd.str().c_str());
#endif
    if (shellCommandFailed(rc)) {
        throw std::runtime_error(
            "blue: npm dependency bundling failed (esbuild). "
            "Check stderr and project imports/package.json.");
    }
}

}  // namespace jsc_npm_bundle
