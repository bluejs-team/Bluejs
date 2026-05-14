#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace jsc_embed_gen {

inline std::string mimeForExtension(const std::filesystem::path& p) {
    std::string e = p.extension().string();
    for (auto& x : e)
        x = (char)tolower((unsigned char)x);
    static const std::map<std::string, std::string> m = {
        {".html", "text/html; charset=utf-8"},
        {".htm", "text/html; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".js", "text/javascript; charset=utf-8"},
        {".mjs", "text/javascript; charset=utf-8"},
        {".json", "application/json; charset=utf-8"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".webp", "image/webp"},
        {".svg", "image/svg+xml"},
        {".woff2", "font/woff2"},
        {".woff", "font/woff"},
        {".ttf", "font/ttf"},
        {".txt", "text/plain; charset=utf-8"}};
    auto it = m.find(e);
    return it != m.end() ? it->second
                         : std::string("application/octet-stream");
}

inline std::string sanitizedSymbol(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (std::isalnum((unsigned char)c))
            o += c;
        else
            o += '_';
    }
    if (o.empty() || std::isdigit((unsigned char)o[0]))
        o.insert(o.begin(), '_');
    return o;
}

struct EmbedFile {
    std::string                     relpath;
    std::filesystem::path absPath;
};

inline std::vector<EmbedFile> listPublicRecursive(
    const std::filesystem::path& pub) {
    std::vector<EmbedFile> items;
    namespace fs           = std::filesystem;
    for (auto it = fs::recursive_directory_iterator(
             pub, fs::directory_options::skip_permission_denied);
         it != fs::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file())
            continue;
        fs::path rel = fs::relative(it->path(), pub);
        std::string rp = rel.generic_string();
        if (rp.find("..") != std::string::npos)
            continue;
        items.push_back(EmbedFile{rp, it->path()});
    }
    std::sort(items.begin(), items.end(),
              [](const EmbedFile& a, const EmbedFile& b) {
                  return a.relpath < b.relpath;
              });
    return items;
}

struct RowParts {
    std::string relpath;
    std::string mimeSym;
    std::string blobSym;
    size_t      nbytes;
};

inline void emitCpp(const std::vector<EmbedFile>& files,
                    const std::filesystem::path& dest) {
    std::ofstream o(dest);
    if (!o)
        throw std::runtime_error("emitCpp: cannot write " + dest.string());

    o << "#include \"runtime/jsc_embed.h\"\n";
    o << "#include <string.h>\n\n";

    std::vector<RowParts> rows;

    for (const EmbedFile& f : files) {
        std::ifstream in(f.absPath, std::ios::binary);
        if (!in)
            throw std::runtime_error("cannot open " + f.absPath.string());
        std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());

        RowParts    rp;
        rp.relpath      = f.relpath;
        std::string sym = sanitizedSymbol(f.relpath);
        rp.mimeSym      = std::string("jsc_emb_mime_") + sym;
        rp.blobSym      = std::string("jsc_emb_blob_") + sym;
        rp.nbytes       = buf.size();
        rows.push_back(rp);

        o << "static const char " << rp.mimeSym << "[] = \"";
        for (unsigned char uc : mimeForExtension(f.absPath)) {
            char ch = (char)uc;
            if (ch == '\"' || ch == '\\')
                o << '\\' << ch;
            else if (ch == '\n')
                o << "\\n";
            else
                o << ch;
        }
        o << "\";\n";

        if (!buf.empty()) {
            o << "static const unsigned char " << rp.blobSym << "[] = {";
            for (size_t i = 0; i < buf.size(); ++i) {
                if (i)
                    o << ",";
                if ((i % 16) == 0)
                    o << "\n  ";
                char hx[24];
                std::snprintf(hx, sizeof(hx), "0x%02x",
                              (unsigned)buf[i]);
                o << hx;
            }
            o << "\n};\n\n";
        }
    }

    o << "typedef struct JSCembRow {\n"
      << "    const char *path;\n"
      << "    const unsigned char *data;\n"
      << "    size_t len;\n"
      << "    const char *mime;\n"
      << "} JSCembRow;\n\n";

    o << "static const JSCembRow jsc_emb_rows[] = {\n";
    for (size_t i = 0; i < rows.size(); ++i) {
        const RowParts& rp = rows[i];
        o << "    { \"";
        for (char c : rp.relpath) {
            if (c == '\\')
                o << "\\\\";
            else if (c == '\"')
                o << "\\\"";
            else if (c == '\n')
                o << "\\n";
            else
                o << c;
        }
        o << "\" , ";
        if (rp.nbytes)
            o << rp.blobSym;
        else
            o << "NULL";
        o << " , (size_t)" << rp.nbytes << " , "
          << rp.mimeSym << " },\n";
    }
    o << "};\n\n";

    o <<
        "extern \"C\" int jsc_embed_lookup(const char *path_in,\n"
        "    const unsigned char **out_data, size_t *out_len,\n"
        "    const char **out_mime) {\n"
        "  if (!path_in || !out_data || !out_len || !out_mime)\n"
        "    return 0;\n"
        "  char needle[8192];\n"
        "  while (*path_in == '/')\n"
        "    ++path_in;\n"
        "  strncpy(needle, path_in, sizeof(needle) - 1);\n"
        "  needle[sizeof(needle) - 1] = 0;\n"
        "  size_t nrow = sizeof(jsc_emb_rows) / sizeof(jsc_emb_rows[0]);\n"
        "  for (size_t i = 0; i < nrow; ++i) {\n"
        "    if (strcmp(jsc_emb_rows[i].path, needle) != 0)\n"
        "      continue;\n"
        "    *out_len = jsc_emb_rows[i].len;\n"
        "    if (jsc_emb_rows[i].len == 0 || jsc_emb_rows[i].data == NULL)\n"
        "      *out_data = (const unsigned char *) \"\";\n"
        "    else\n"
        "      *out_data = jsc_emb_rows[i].data;\n"
        "    *out_mime = jsc_emb_rows[i].mime;\n"
        "    return 1;\n"
        "  }\n"
        "  return 0;\n"
        "}\n";
}

} /* namespace jsc_embed_gen */
