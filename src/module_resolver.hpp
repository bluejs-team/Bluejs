#pragma once
/*
 * module_resolver.hpp - CommonJS require() dependency resolution for Blue.
 *
 * Walks require('./foo') calls in the AST, parses each referenced file,
 * and returns a topologically-ordered list of Module objects (deps first,
 * entry last).  Each file body is run through Babel (preset-env) then Esprima.
 * Built-in module names (fs, path, os, …) are recognised but not resolved as
 * files; they are handled by js_require_builtin() at runtime.
 */

#include "ast_node.hpp"
#include "babel_transform.hpp"
#include "parser.hpp"
#include "quickjs_compat.h"
#include <nlohmann/json.hpp>

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cctype>

// ── Module record ──────────────────────────────────────────────────────────

struct Module {
    std::string filePath;   // resolved file path used as the map key
    std::string cPrefix;    // safe C identifier prefix, e.g. "mod_utils"
    ASTNode     ast;        // parsed AST root (Program)

    // exports: JS-exported name → C identifier (function name or static var)
    //   e.g. { "add" → "mod_utils_add", "answer" → "mod_mpropv_answer" }
    // "default" key is used for `module.exports = singleIdent` exports.
    std::map<std::string, std::string> exports;

    // Which export cNames are C FunctionDeclaration symbols (not JsValue vars).
    // Used by the emitter to decide what static vars to declare.
    std::set<std::string> functionCNames;
};

// ── ModuleResolver ─────────────────────────────────────────────────────────

class ModuleResolver {
public:
    // Resolve all transitive require() dependencies of entryPath.
    // Esprima and @babel/standalone must already be loaded in ctx.
    // Returns modules in topological order: deps before the entry that uses them.
    static std::vector<Module> resolve(const std::string& entryPath,
                                       JSContext* ctx) {
        ModuleResolver r(ctx);
        r.visit(entryPath);
        return r.ordered_;
    }

    // Resolve a relative require path to a file path.
    // fromFile: path of the file that contains the require() call.
    // req:      the require() argument string (e.g. "./utils" or "../lib/foo").
    static std::string resolvePath(const std::string& fromFile,
                                   const std::string& req) {
        std::string dir;
        auto slash = fromFile.rfind('/');
        if (slash != std::string::npos) dir = fromFile.substr(0, slash);
        else                            dir = ".";

        std::string path = dir + "/" + req;
        if (path.size() < 3 || path.substr(path.size() - 3) != ".js")
            path += ".js";
        return path;
    }

    // True if the module name is a Node.js / Electron built-in.
    static bool isBuiltin(const std::string& name) {
        static const char* builtins[] = {
            "fs", "path", "os", "process", "buffer", "electron", "events",
            "util", "assert", "url", "child_process", "stream",
            "crypto", "http", "https", "net", "tls", nullptr
        };
        for (int i = 0; builtins[i]; ++i)
            if (name == builtins[i]) return true;
        return false;
    }

    /** For hybrid AOT: bare require('pkg') must be builtins only (npm → island). */
    static void validateHybridAotGraph(const std::vector<Module>& modules) {
        for (const Module& mod : modules) {
            std::vector<std::string> reqs;
            scanRequires(mod.ast, reqs);
            for (const std::string& req : reqs) {
                if (req.size() >= 2 &&
                    (req.substr(0, 2) == "./" || req.substr(0, 3) == "../"))
                    continue;
                if (isBuiltin(req))
                    continue;
                throw std::runtime_error(
                    "❌ Blue Build Error: NPM Contamination\n"
                    "You attempted to import \"" + req +
                    "\" inside an AOT file (" + mod.filePath + ").\n"
                    "The Fix: The AOT core only supports relative local files "
                    "(e.g., ./math.js). Move all NPM dependencies to your "
                    "src/island.js file.");
            }
            scanHybridAotDynamicExecution(mod.ast, mod.filePath);
        }
    }

    /** Non-hybrid `-compile`/`-build` graphs: disallow eval/dynamic-import in AOT AST. */
    static void validateAotDynamicExecution(const std::vector<Module>& modules) {
        for (const Module& mod : modules)
            scanHybridAotDynamicExecution(mod.ast, mod.filePath);
    }

private:
    JSContext*            ctx_;
    std::set<std::string> visited_;
    std::vector<Module>   ordered_;
    std::map<std::string, int> prefixCount_;

    explicit ModuleResolver(JSContext* ctx) : ctx_(ctx) {}

    static std::string parseScriptToAstJson(JSContext* ctx,
                                            const std::string& src,
                                            const std::string& pathForErrors) {
        std::string encoded = nlohmann::json(src).dump();
        std::string expr =
            "JSON.stringify(esprima.parseScript(" + encoded + "))";
        JSValue v = JS_Eval(ctx, expr.data(), expr.size(), "<parse>",
                            JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(v)) {
            JS_FreeValue(ctx, v);
            JSValue ex = JS_GetException(ctx);
            const char* msg = JS_ToCString(ctx, ex);
            std::string err = "blue: parse error in " + pathForErrors + ": ";
            err += (msg ? msg : "exception");
            if (msg)
                JS_FreeCString(ctx, msg);
            JS_FreeValue(ctx, ex);
            throw std::runtime_error(err);
        }
        if (!JS_IsString(v)) {
            JS_FreeValue(ctx, v);
            throw std::runtime_error(
                "blue: internal error: JSON.stringify did not return a string");
        }
        const char* astJson = JS_ToCString(ctx, v);
        std::string out = astJson ? astJson : "";
        if (astJson)
            JS_FreeCString(ctx, astJson);
        JS_FreeValue(ctx, v);
        return out;
    }

    ASTNode parseFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open())
            throw std::runtime_error("blue: cannot open: " + path);
        std::ostringstream buf;
        buf << f.rdbuf();
        std::string raw = buf.str();
        /*
         * Reject obviously-dynamic import() calls before lowering. Cannot use naive
         * eval( substring scans - inlined frontend bundles legitimately embed that text.
         */
        roughValidateDynamicImportSource(raw, path);
        std::string src = babelTransformSource(ctx_, raw, path);

        std::string astJson = parseScriptToAstJson(ctx_, src, path);
        nlohmann::json j = nlohmann::json::parse(astJson);
        return Parser::parseNode(j);
    }

    /*
     * import(spec) where spec is non-literal - scan sources while skipping strings
     * and comments so inlined bundle text does not trip the heuristic.
     */
    static bool roughIsIdentTail(unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '$';
    }

    static void roughValidateDynamicImportSource(const std::string& src,
                                                 const std::string& filePath) {
        enum class LexState { NORMAL, LINE_COMMENT, BLOCK_COMMENT };
        LexState lx = LexState::NORMAL;
        auto throwDyn = [&filePath]() {
            throw std::runtime_error(
                "❌ Blue Build Error: Dynamic Execution\n"
                "You used a dynamic execution command in an AOT file (" +
                filePath + "). Ahead-Of-Time compilers cannot guess what code "
                "will run at runtime.\n"
                "The Fix: Move this logic to the QuickJS island, or rewrite it "
                "using static logic.");
        };

        auto is_sq_dq_esc = [&](size_t quoteStart, size_t pos) {
            bool odd = false;
            for (size_t p = pos; p > quoteStart && src[p - 1] == '\\'; --p)
                odd = !odd;
            return odd;
        };

        for (size_t i = 0; i < src.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(src[i]);

            if (lx == LexState::LINE_COMMENT) {
                if (c == '\n')
                    lx = LexState::NORMAL;
                continue;
            }
            if (lx == LexState::BLOCK_COMMENT) {
                if (c == '*' && i + 1 < src.size() && src[i + 1] == '/') {
                    lx = LexState::NORMAL;
                    ++i;
                }
                continue;
            }

            /* Normal code: skip line comments, block comments, and quoted strings. */
            if (c == '/' && i + 1 < src.size()) {
                if (src[i + 1] == '/') {
                    lx = LexState::LINE_COMMENT;
                    ++i;
                    continue;
                }
                if (src[i + 1] == '*') {
                    lx = LexState::BLOCK_COMMENT;
                    ++i;
                    continue;
                }
            }
            if (c == '\'') {
                size_t q = i;
                for (++i; i < src.size(); ++i) {
                    if (src[i] == '\'' && !is_sq_dq_esc(q, i))
                        break;
                }
                continue;
            }
            if (c == '"') {
                size_t q = i;
                for (++i; i < src.size(); ++i) {
                    if (src[i] == '"' && !is_sq_dq_esc(q, i))
                        break;
                }
                continue;
            }
            if (c == '`') {
                size_t q = i;
                for (++i; i < src.size(); ++i) {
                    if (src[i] == '`' && !is_sq_dq_esc(q, i))
                        break;
                }
                continue;
            }

            if (src.compare(i, 6, "import") != 0)
                continue;
            if (i > 0 && roughIsIdentTail(static_cast<unsigned char>(src[i - 1])))
                continue;
            if (i + 6 < src.size() &&
                roughIsIdentTail(static_cast<unsigned char>(src[i + 6])))
                continue;

            size_t j = i + 6;
            while (j < src.size() && std::isspace(static_cast<unsigned char>(src[j])))
                ++j;
            if (j >= src.size() || src[j] != '(')
                continue;
            ++j;
            while (j < src.size() && std::isspace(static_cast<unsigned char>(src[j])))
                ++j;
            if (j < src.size() &&
                (src[j] == '"' || src[j] == '\'' || src[j] == '`'))
                continue;
            throwDyn();
        }
    }

    static std::string basePrefix(const std::string& path) {
        std::string base = path;
        auto slash = base.rfind('/');
        if (slash != std::string::npos) base = base.substr(slash + 1);
        auto dot = base.rfind('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        std::string result = "mod_";
        for (char c : base)
            result += (std::isalnum((unsigned char)c) ? c : '_');
        return result;
    }

    std::string uniquePrefix(const std::string& base) {
        auto& cnt = prefixCount_[base];
        if (cnt == 0) { cnt = 1; return base; }
        return base + "_" + std::to_string(cnt++);
    }

    static void scanRequires(const ASTNode& node,
                             std::vector<std::string>& out) {
        if (node.type == "CallExpression" && !node.children.empty()) {
            const ASTNode& callee = node.children[0];
            if (callee.type == "Identifier" &&
                callee.data.value("name", "") == "require" &&
                node.children.size() >= 2) {
                const ASTNode& arg = node.children[1];
                if (arg.type == "Literal" &&
                    arg.data.contains("value") &&
                    arg.data["value"].is_string()) {
                    out.push_back(arg.data["value"].get<std::string>());
                }
            }
        }
        for (const auto& c : node.children) scanRequires(c, out);
    }

    static bool isEvalCall(const ASTNode& node) {
        if (node.type != "CallExpression" || node.children.empty())
            return false;
        const ASTNode& callee = node.children[0];
        if (callee.type == "Identifier" &&
            callee.data.value("name", "") == "eval")
            return true;
        if (callee.type == "MemberExpression" && callee.children.size() >= 2) {
            const ASTNode& prop = callee.children[1];
            if (prop.type == "Identifier" &&
                prop.data.value("name", "") == "eval")
                return true;
        }
        return false;
    }

    static bool isDynamicImportCall(const ASTNode& node) {
        if (node.type != "ImportExpression")
            return false;
        if (node.children.empty())
            return true;
        const ASTNode& arg = node.children[0];
        if (arg.type == "Literal" &&
            arg.data.contains("value") &&
            arg.data["value"].is_string())
            return false;
        return true;
    }

    static void scanHybridAotDynamicExecution(const ASTNode& node,
                                              const std::string& filePath) {
        if (isEvalCall(node) || isDynamicImportCall(node)) {
            throw std::runtime_error(
                "❌ Blue Build Error: Dynamic Execution\n"
                "You used a dynamic execution command in an AOT file (" +
                filePath + "). Ahead-Of-Time compilers cannot guess what code "
                "will run at runtime.\n"
                "The Fix: Move this logic to the QuickJS island, or rewrite it "
                "using static logic.");
        }
        for (const auto& c : node.children)
            scanHybridAotDynamicExecution(c, filePath);
    }

    static void extractExports(const ASTNode& ast, Module& mod) {
        for (const auto& stmt : ast.children) {
            if (stmt.type == "FunctionDeclaration" && !stmt.children.empty()) {
                std::string name = stmt.children[0].data.value("name", "");
                if (!name.empty()) {
                    std::string cName = mod.cPrefix + "_" + name;
                    mod.exports[name] = cName;
                    mod.functionCNames.insert(cName);
                }
            }
        }

        for (const auto& stmt : ast.children) {
            if (stmt.type != "ExpressionStatement" || stmt.children.empty())
                continue;
            const ASTNode& expr = stmt.children[0];
            if (expr.type != "AssignmentExpression" || expr.children.size() < 2)
                continue;
            const ASTNode& lhs = expr.children[0];
            const ASTNode& rhs = expr.children[1];

            if (isModuleExports(lhs)) {
                mod.exports.clear();
                applyRhsExports(rhs, mod);
            } else if (isModuleExportsProp(lhs)) {
                std::string key = propKey(lhs.children[1]);
                if (!key.empty()) {
                    std::string cSuffix = key;
                    if (rhs.type == "Identifier") {
                        std::string v = rhs.data.value("name", "");
                        if (!v.empty()) cSuffix = v;
                    }
                    mod.exports[key] = mod.cPrefix + "_" + cSuffix;
                }
            }
        }
    }

    static void applyRhsExports(const ASTNode& rhs, Module& mod) {
        if (rhs.type == "ObjectExpression") {
            for (const auto& prop : rhs.children) {
                if (prop.type != "Property" || prop.children.empty()) continue;
                std::string key = propKey(prop.children[0]);
                if (key.empty()) continue;
                std::string valIdent = key;
                if (prop.children.size() >= 2 &&
                    prop.children[1].type == "Identifier") {
                    std::string v = prop.children[1].data.value("name", "");
                    if (!v.empty()) valIdent = v;
                }
                mod.exports[key] = mod.cPrefix + "_" + valIdent;
            }
        } else if (rhs.type == "Identifier") {
            std::string name = rhs.data.value("name", "");
            if (!name.empty()) {
                mod.exports["default"] = mod.cPrefix + "_" + name;
                mod.exports[name]      = mod.cPrefix + "_" + name;
            }
        }
    }

    static bool isModuleExports(const ASTNode& node) {
        if (node.type != "MemberExpression" || node.children.size() < 2)
            return false;
        return node.children[0].data.value("name", "") == "module" &&
               node.children[1].data.value("name", "") == "exports";
    }

    static bool isModuleExportsProp(const ASTNode& node) {
        if (node.type != "MemberExpression" || node.children.size() < 2)
            return false;
        return isModuleExports(node.children[0]);
    }

    static std::string propKey(const ASTNode& node) {
        if (node.type == "Identifier")
            return node.data.value("name", "");
        if (node.type == "Literal" && node.data.contains("value") &&
            node.data["value"].is_string())
            return node.data["value"].get<std::string>();
        return "";
    }

    void visit(const std::string& path) {
        if (visited_.count(path)) return;
        visited_.insert(path);

        Module mod;
        mod.filePath = path;
        mod.cPrefix  = uniquePrefix(basePrefix(path));
        mod.ast      = parseFile(path);

        std::vector<std::string> rawReqs;
        scanRequires(mod.ast, rawReqs);
        for (const auto& req : rawReqs) {
            if (req.size() >= 2 &&
                (req.substr(0, 2) == "./" || req.substr(0, 3) == "../")) {
                visit(resolvePath(path, req));
            }
        }

        extractExports(mod.ast, mod);
        ordered_.push_back(std::move(mod));
    }
};
