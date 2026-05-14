#pragma once

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

static std::string emitControlPlaneCpp(const std::vector<uint8_t>& bundle) {
    std::ostringstream o;
    o << "/* blue generated: control plane (QuickJS + native I/O) */\n";
    o << "#define JSC_RUNTIME_QUICKJS 1\n";
    o << "#define JSC_RUNTIME_UV 1\n";
    o << "#define JSC_HAVE_UV 1\n";
    o << "#define JSC_RUNTIME_NODE_IO 1\n";
    o << "#define JSC_QJS_CONTROL_PLANE 1\n";
    o << "#include \"runtime/jsc_runtime.h\"\n";
    o << "#include <stddef.h>\n";
    o << "static const unsigned char jsc_control_bundle[] = {";
    for (size_t i = 0; i < bundle.size(); ++i) {
        if (i)
            o << ',';
        if (i % 20 == 0)
            o << '\n';
        o << "0x" << std::hex << std::setw(2) << std::setfill('0')
          << (unsigned)bundle[i];
    }
    if (!bundle.empty())
        o << ',';
    o << "0x00\n};\nstatic const size_t jsc_control_bundle_len = " << std::dec
      << bundle.size() << ";\n";
    o << "int main() {\n";
    o << "  jsc_control_plane_run_embedded(jsc_control_bundle, "
         "jsc_control_bundle_len);\n";
    o << "  jsc_uv_run_loop_forever();\n";
    o << "  return 0;\n}\n";
    return o.str();
}

static std::string emitHybridIslandBlob(const std::vector<uint8_t>& bundle) {
    std::ostringstream o;
    o << "/* blue generated: embedded QuickJS npm island (true hybrid) */\n";
    o << "static const unsigned char jsc_hybrid_island_bundle[] = {";
    for (size_t i = 0; i < bundle.size(); ++i) {
        if (i)
            o << ',';
        if (i % 20 == 0)
            o << '\n';
        o << "0x" << std::hex << std::setw(2) << std::setfill('0')
          << (unsigned)bundle[i];
    }
    if (!bundle.empty())
        o << ',';
    o << "0x00\n};\nstatic const unsigned long long jsc_hybrid_island_bundle_len = "
      << std::dec << bundle.size() << "ULL;\n";
    return o.str();
}
