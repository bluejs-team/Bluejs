#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

static void cmdInit(const fs::path& dir) {
    fs::create_directories(dir / "public");
    fs::create_directories(dir / "src");

    nlohmann::json cfg;
    cfg["entry"]              = "src/main.js";
    cfg["quickjsIsland"]      = "src/island.js";
    cfg["hybrid"]             = true;
    cfg["bundlePlatform"]     = "node";
    cfg["publicDir"]          = "public";
    cfg["bundleDependencies"] = false;
    cfg["controlPlane"]       = false;
    cfg["bundleHtml"] =
        std::vector<std::string>{"inline public/index.html for dev"};
    std::ofstream ac(dir / "blue.config.json");
    ac << cfg.dump(2) << "\n";

    const char* css = "body{font-family:sans-serif;margin:48px;background:#0f172a;"
                      "color:#e2e8f0;}h1{color:#38bdf8;}\n";
    std::ofstream style(dir / "public" / "style.css");
    style << css;

    const char* html =
        "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"/>\n"
        "<link rel=\"stylesheet\" href=\"style.css\"/></head><body>"
        "<h1>blue GUI</h1>"
        "<p>Native bridge: <code>fetch('blue://app/__bridge__/'+encodeURIComponent(JSON."
        "stringify({cmd:\"ping\"})))</code> then listen for <code>blue-backend</code>.</p>"
        "<button id=\"b\">Ping backend</button><pre id=\"o\"></pre>"
        "<img alt=\"demo\" src=\"blue://app/app.svg\"/>"
        "<script>\n"
        "var o=document.getElementById('o');function L(m){o.textContent+=m+'\\n';}\n"
        "window.addEventListener('blue-backend',function(e){\n"
        " var d=e.detail||{};var s=d.payloadB64?atob(d.payloadB64):'';\n"
        " L('reply '+JSON.stringify(d)+' '+s);});\n"
        "document.getElementById('b').onclick=function(){\n"
        " var p=encodeURIComponent(JSON.stringify({cmd:'ping'}));\n"
        " fetch('blue://app/__bridge__/'+p).then(function(r){return r.text();})\n"
        " .then(function(t){L('fetch '+t);}).catch(function(e){L(String(e));});};\n"
        "</script></body></html>\n";
    std::ofstream idx(dir / "public" / "index.html");
    idx << html;

    const char* svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                      "width=\"120\" "
                      "height=\"80\"><rect fill=\"#38bdf8\" width=\"120\" "
                      "height=\"80\""
                      "/><text x=\"12\" "
                      "y=\"48\" "
                      "font-size=\"20\" "
                      "fill=\"#0f172a\">SVG</text></svg>\n";
    std::ofstream svgf(dir / "public" / "app.svg");
    svgf << svg;

    const char* mj =
        "\"use strict\";\n"
        "var html = __BLUE_BUNDLE_HTML__;\n"
        "window.open(html, \"Blue App\", 900, 600);\n";
    std::ofstream m(dir / "src" / "main.js");
    m << mj;

    const char* islandJs =
        "\"use strict\";\n"
        "const http = require(\"http\");\n\n"
        "const server = http.createServer((req, res) => {\n"
        "  if (req.url === \"/\") {\n"
        "    res.statusCode = 200;\n"
        "    res.setHeader(\"content-type\", \"text/plain; charset=utf-8\");\n"
        "    res.end(\"hello from blue island\");\n"
        "    return;\n"
        "  }\n"
        "  res.statusCode = 404;\n"
        "  res.end(\"not found\");\n"
        "});\n\n"
        "server.listen(48313, \"127.0.0.1\", () => {\n"
        "  console.log(\"blue island listening on http://127.0.0.1:48313\");\n"
        "});\n";
    std::ofstream is(dir / "src" / "island.js");
    is << islandJs;

    std::cerr << "blue: scaffolded project in "
              << fs::absolute(dir).generic_string() << "\n";
}
