#pragma once
/*
 * Run user/module JavaScript through @babel/standalone (preset-env) before
 * Esprima parses it - lowers modern syntax so the rest of blue sees a simpler AST.
 *
 * Requires Babel to be loaded in the QuickJS global scope (see main.cpp).
 * Set JSC_NO_BABEL=1 to skip transformation (debugging).
 */

#include "quickjs_compat.h"
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

inline std::string babelTransformSource(JSContext* ctx, const std::string& src,
                                        const std::string& pathForErrors) {
    const char* no = std::getenv("JSC_NO_BABEL");
    if (no && no[0] != '\0' && std::strcmp(no, "0") != 0)
        return src;

    std::string encoded = nlohmann::json(src).dump();
    std::string expr =
        "(function(){var s=" + encoded +
        ";"
        " if (typeof Babel==='undefined') return s;"
        " try {"
        " return Babel.transform(s,{"
        " presets:[['env',{targets:{ie:'11'},loose:true}]],"
        " compact:false"
        " }).code;"
        " } catch(e){"
        " throw e;"
        " }"
        "})()";

    JSValue v =
        JS_Eval(ctx, expr.data(), expr.size(), "<babel>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        JS_FreeValue(ctx, v);
        JSValue ex = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, ex);
        std::string err =
            "blue: Babel transform failed for " + pathForErrors + ": ";
        err += msg ? msg : "exception";
        if (msg)
            JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, ex);
        throw std::runtime_error(err);
    }
    if (!JS_IsString(v)) {
        JS_FreeValue(ctx, v);
        throw std::runtime_error(
            "blue: internal error: Babel.transform did not return a string for " +
            pathForErrors);
    }
    const char* out = JS_ToCString(ctx, v);
    std::string ret = out ? out : "";
    if (out)
        JS_FreeCString(ctx, out);
    JS_FreeValue(ctx, v);
    return ret;
}
