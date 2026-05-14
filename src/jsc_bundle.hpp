#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace jsc_bundle {

namespace fs = std::filesystem;

inline std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot read: " + p.string());
    std::ostringstream b;
    b << f.rdbuf();
    return b.str();
}

inline bool pathUnderPublishRoot(const fs::path& publishRoot,
                                 const fs::path& cand) {
    try {
        fs::path rel =
            fs::relative(fs::weakly_canonical(cand),
                         fs::weakly_canonical(publishRoot));
        std::string s = rel.generic_string();
        if (s.empty() || s == ".")
            return true;
        /* fs::relative returns paths starting with ".." when outside root */
        return s.compare(0, 3, "../") != 0 && s != "..";
    } catch (...) {
        return false;
    }
}

/* Join publicRoot/dir/href safely (reject '..'). Returns empty if bogus. */
inline fs::path publicChild(const fs::path& publicRoot,
                            const fs::path& htmlDir,
                            const std::string& href) {
    if (href.empty() || href[0] == '/' || href.rfind("//", 0) == 0)
        return {};
    fs::path rel = fs::proximate(htmlDir, publicRoot);
    fs::path p   = (rel / fs::path(href)).lexically_normal();
    if (std::find(p.begin(), p.end(), fs::path("..")) != p.end())
        return {};
    fs::path out = (publicRoot / p).lexically_normal();
    if (!fs::exists(out))
        return {};
    return pathUnderPublishRoot(publicRoot, out) ? out : fs::path();
}

/* Inline stylesheet <link> and external <script src> tags for index.html under public/. */
inline std::string inlineHtmlDeps(const fs::path& htmlPath,
                                  const fs::path& publicRoot) {
    static const std::regex linkRe(R"(<\s*link([^>]*?)\/\s*>|<\s*link([^>*?]*)>)",
                                   std::regex::icase);
    static const std::regex scriptRe(
        R"(<\s*script([^>]*?)\/\s*>|<\s*script\b([^>]*)>)", std::regex::icase);
    static const std::regex hrefAttr(R"(href\s*=\s*["']([^"']+)["'])",
                                     std::regex::icase);
    static const std::regex srcAttr(R"(src\s*=\s*["']([^"']+)["'])",
                                    std::regex::icase);
    static const std::regex relCss(R"(rel\s*=\s*["']stylesheet["'])",
                                   std::regex::icase);

    std::string         out      = readAll(htmlPath);
    const fs::path      htmlDir  = htmlPath.parent_path();
restart:
    for (auto it = std::sregex_iterator(out.begin(), out.end(), linkRe);
         it != std::sregex_iterator(); ++it) {
        std::string tag       = (*it)[0];
        std::string inner     = ((*it)[1].length() ? (*it)[1].str()
                                                   : (*it)[2].str());
        if (!std::regex_search(inner, relCss))
            continue;
        std::smatch ha;
        if (!std::regex_search(inner, ha, hrefAttr))
            continue;
        fs::path cand =
            publicChild(publicRoot, htmlDir, ha.str(1));
        if (cand.empty() || !fs::exists(cand))
            continue;
        auto pos = out.find(tag);
        if (pos == std::string::npos)
            continue;
        std::string css   = readAll(cand);
        std::string block = "<style>/* ";
        block += cand.filename().string();
        block += " */\n";
        block += css;
        block += "</style>";
        out.replace(pos, tag.size(), block);
        goto restart;
    }

restart_js:
    for (auto it = std::sregex_iterator(out.begin(), out.end(), scriptRe);
         it != std::sregex_iterator(); ++it) {
        std::string tag   = (*it)[0];
        std::string inner = ((*it)[1].length() ? (*it)[1].str()
                                               : (*it)[2].str());
        std::smatch sa;
        if (!std::regex_search(inner, sa, srcAttr))
            continue;
        fs::path cand =
            publicChild(publicRoot, htmlDir, sa.str(1));
        if (cand.empty() || !fs::exists(cand))
            continue;
        auto pos = out.find(tag);
        if (pos == std::string::npos)
            continue;
        std::string js = readAll(cand);
        std::string block =
            "<script>/* " + cand.filename().string() + " */\n" +
            js + "</script>";
        out.replace(pos, tag.size(), block);
        goto restart_js;
    }

    return out;
}

} /* namespace jsc_bundle */
