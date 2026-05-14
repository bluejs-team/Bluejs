#!/usr/bin/env bash
# Precompile Blue runtime sources into static libraries.
#
# Usage (from compiler repo root):
#   bash build/precompile-runtime.sh           # Linux
#   bash build/precompile-runtime.sh --windows # Windows (MinGW cross-compile)
#
# Produces three archives (Linux) or two (Windows):
#   libblue_runtime.a         - base: stubs + QuickJS fallback + native core
#   libblue_runtime_hybrid.a  - UV+QuickJS real implementations (Linux only)
#   libblue_runtime_webview.a - real webview/native platform implementations (Linux only)
#
# Linking rules in blue_bin:
#   AOT-only:   libblue_runtime.a + quickjs objects
#   Hybrid/UV:  --start-group libblue_runtime_hybrid.a libblue_runtime.a --end-group + quickjs
#   WebView:    --start-group libblue_runtime_webview.a libblue_runtime.a --end-group
#               (webview archive overrides stubs in base via first-definition-wins)
#   WebView+assets: same as above + source-compile generated jsc_assets.cpp

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WINDOWS=0
if [[ "${1:-}" == "--windows" ]]; then
  WINDOWS=1
fi

if [[ $WINDOWS -eq 1 ]]; then
  CXX="${MINGW_CXX:-x86_64-w64-mingw32-g++}"
  AR="${MINGW_AR:-x86_64-w64-mingw32-ar}"
  OUT_BASE="${ROOT}/src/runtime/libblue_runtime_win.a"
  echo "Precompiling Windows runtime with ${CXX} …"
else
  CXX="${CXX:-c++}"
  AR="${AR:-ar}"
  OUT_BASE="${ROOT}/src/runtime/libblue_runtime.a"
  OUT_HYBRID="${ROOT}/src/runtime/libblue_runtime_hybrid.a"
  OUT_WEBVIEW="${ROOT}/src/runtime/libblue_runtime_webview.a"
  echo "Precompiling Linux runtime with ${CXX} …"
fi

if ! command -v "${CXX}" >/dev/null 2>&1; then
  echo "Compiler not found: ${CXX}" >&2
  if [[ $WINDOWS -eq 1 ]]; then
    echo "Install with: sudo apt install g++-mingw-w64-x86-64" >&2
  fi
  exit 1
fi

STAGE="$(mktemp -d "${TMPDIR:-/tmp}/blue-rt.XXXXXX")"
STAGE_HYBRID="$(mktemp -d "${TMPDIR:-/tmp}/blue-rth.XXXXXX")"
STAGE_WV="$(mktemp -d "${TMPDIR:-/tmp}/blue-wv.XXXXXX")"
cleanup() { rm -rf "$STAGE" "$STAGE_HYBRID" "$STAGE_WV"; }
trap cleanup EXIT

# Base flags (no UV, QuickJS enabled for fallback)
BASE_FLAGS="-std=c++17 -O2 -ffunction-sections -fdata-sections"
BASE_FLAGS="${BASE_FLAGS} -DJSC_RUNTIME_QUICKJS=1 -DJSC_QJS_CONTROL_PLANE=1 -DJSC_HYBRID=1"
BASE_FLAGS="${BASE_FLAGS} -I\"${ROOT}/src\" -I\"${ROOT}/vendor/quickjs\""

# Hybrid flags (full UV + QuickJS)
HYBRID_FLAGS="-std=c++17 -O2 -ffunction-sections -fdata-sections"
HYBRID_FLAGS="${HYBRID_FLAGS} -DJSC_RUNTIME_QUICKJS=1 -DJSC_RUNTIME_UV=1 -DJSC_HAVE_UV=1"
HYBRID_FLAGS="${HYBRID_FLAGS} -DJSC_RUNTIME_NODE_IO=1 -DJSC_QJS_CONTROL_PLANE=1 -DJSC_HYBRID=1"
HYBRID_FLAGS="${HYBRID_FLAGS} -I\"${ROOT}/src\" -I\"${ROOT}/vendor/quickjs\""

# Suppress warnings during precompile - runtime sources aren't our code to fix
BASE_FLAGS="${BASE_FLAGS} -w -Wno-error"
HYBRID_FLAGS="${HYBRID_FLAGS} -w -Wno-error"

compile_one() {
  local src="$1"
  local outdir="$2"
  local flags="$3"
  local extra_flags="${4:-}"
  local base
  base="$(basename "${src}" .cpp)"
  base="$(basename "${base}" .mm)"
  local obj="${outdir}/${base}.o"
  eval "\"${CXX}\" ${flags} ${extra_flags} -c \"${src}\" -o \"${obj}\""
  echo "  compiled $(basename ${src})"
}

# ── Base archive ──────────────────────────────────────────────────────────────
echo "Building base archive …"
compile_one "${ROOT}/src/runtime/jsc_node_io.cpp"             "${STAGE}" "${BASE_FLAGS}"
compile_one "${ROOT}/src/runtime/jsc_event_loop.cpp"           "${STAGE}" "${BASE_FLAGS}"
compile_one "${ROOT}/src/runtime/jsc_quickjs_fallback.cpp"     "${STAGE}" "${BASE_FLAGS}"
compile_one "${ROOT}/src/runtime/jsc_blue_plugin.cpp"          "${STAGE}" "${BASE_FLAGS}"
compile_one "${ROOT}/src/runtime/jsc_blue_native_core.cpp"     "${STAGE}" "${BASE_FLAGS}"
compile_one "${ROOT}/src/runtime/jsc_webview_stub.cpp"         "${STAGE}" "${BASE_FLAGS}"
compile_one "${ROOT}/src/runtime/jsc_embed_stub.cpp"           "${STAGE}" "${BASE_FLAGS}"
compile_one "${ROOT}/src/runtime/jsc_blue_native_gui_stub.cpp" "${STAGE}" "${BASE_FLAGS}"

rm -f "${OUT_BASE}"
"${AR}" rcs "${OUT_BASE}" "${STAGE}"/*.o
echo "Wrote ${OUT_BASE} ($(du -sh "${OUT_BASE}" | cut -f1))"

if [[ $WINDOWS -eq 1 ]]; then
  # ── Windows webview archive ──────────────────────────────────────────────────
  OUT_WEBVIEW_WIN="${ROOT}/src/runtime/libblue_runtime_webview_win.a"
  echo "Building Windows webview archive …"
  STAGE_WV_WIN="$(mktemp -d "${TMPDIR:-/tmp}/blue-wv-win.XXXXXX")"
  WV_WIN_FLAGS="${BASE_FLAGS}"
  WEBVIEW2_SDK="${WEBVIEW2_SDK_PATH:-${ROOT}/vendor/webview2}"
  if [[ -f "${WEBVIEW2_SDK}/build/native/include/WebView2.h" ]]; then
    if [[ ! -f "${WEBVIEW2_SDK}/build/native/include/EventToken.h" ]]; then
      printf '#pragma once\n#include <eventtoken.h>\n' > "${WEBVIEW2_SDK}/build/native/include/EventToken.h"
    fi
    WV_WIN_FLAGS="${WV_WIN_FLAGS} -DBLUE_HAVE_WEBVIEW2=1 -I\"${WEBVIEW2_SDK}/build/native/include\""
    echo "  WebView2 SDK enabled: ${WEBVIEW2_SDK}"
  else
    echo "  WebView2 SDK not found; building MessageBox fallback."
  fi
  compile_one "${ROOT}/src/runtime/windows/jsc_webview_win.cpp"     "${STAGE_WV_WIN}" "${WV_WIN_FLAGS}"
  compile_one "${ROOT}/src/runtime/windows/jsc_blue_native_win.cpp" "${STAGE_WV_WIN}" "${WV_WIN_FLAGS}"
  rm -f "${OUT_WEBVIEW_WIN}"
  "${AR}" rcs "${OUT_WEBVIEW_WIN}" "${STAGE_WV_WIN}"/*.o
  echo "Wrote ${OUT_WEBVIEW_WIN} ($(du -sh "${OUT_WEBVIEW_WIN}" | cut -f1))"
  rm -rf "${STAGE_WV_WIN}"

  # ── Windows hybrid archive (UV + QuickJS) ────────────────────────────────────
  # Requires libuv cross-compiled for Windows; build it if not present.
  LIBUV_WIN_DIR="${ROOT}/vendor/libuv-win"
  if [[ ! -f "${LIBUV_WIN_DIR}/lib/libuv.a" ]]; then
    echo "Building libuv for Windows (MinGW cross-compile) …"
    LIBUV_SRC="${TMPDIR:-/tmp}/libuv-src"
    if [[ ! -d "${LIBUV_SRC}" ]]; then
      curl -fsSL https://dist.libuv.org/dist/v1.49.2/libuv-v1.49.2.tar.gz | tar -xz -C "${TMPDIR:-/tmp}"
      mv "${TMPDIR:-/tmp}/libuv-v1.49.2" "${LIBUV_SRC}"
    fi
    mkdir -p "${LIBUV_WIN_DIR}"
    cmake "${LIBUV_SRC}" -B "${LIBUV_SRC}/build-win" \
      -DCMAKE_TOOLCHAIN_FILE="${ROOT}/build/mingw-toolchain.cmake" \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=OFF \
      -DLIBUV_BUILD_TESTS=OFF -DLIBUV_BUILD_BENCH=OFF \
      -DCMAKE_INSTALL_PREFIX="${LIBUV_WIN_DIR}" -DCMAKE_INSTALL_LIBDIR=lib 2>/dev/null
    cmake --build "${LIBUV_SRC}/build-win" --target uv_a -j"$(nproc)"
    mkdir -p "${LIBUV_WIN_DIR}/lib" "${LIBUV_WIN_DIR}/include"
    cp "${LIBUV_SRC}/build-win/libuv.a" "${LIBUV_WIN_DIR}/lib/"
    cp -r "${LIBUV_SRC}/include/." "${LIBUV_WIN_DIR}/include/"
    echo "libuv built at ${LIBUV_WIN_DIR}"
  fi

  OUT_HYBRID_WIN="${ROOT}/src/runtime/libblue_runtime_hybrid_win.a"
  STAGE_HYB_WIN="$(mktemp -d "${TMPDIR:-/tmp}/blue-hyb-win.XXXXXX")"
  WIN_HYBRID_FLAGS="-std=c++17 -O2 -w -ffunction-sections -fdata-sections"
  WIN_HYBRID_FLAGS="${WIN_HYBRID_FLAGS} -DJSC_RUNTIME_QUICKJS=1 -DJSC_RUNTIME_UV=1 -DJSC_HAVE_UV=1"
  WIN_HYBRID_FLAGS="${WIN_HYBRID_FLAGS} -DJSC_RUNTIME_NODE_IO=1 -DJSC_QJS_CONTROL_PLANE=1 -DJSC_HYBRID=1"
  # UV_STATIC_BUILD: tells uv.h to not use __declspec(dllimport), so objects reference
  # uv_* directly instead of __imp_uv_*. Required when linking against static libuv.a.
  WIN_HYBRID_FLAGS="${WIN_HYBRID_FLAGS} -DUV_STATIC_BUILD=1"
  WIN_HYBRID_FLAGS="${WIN_HYBRID_FLAGS} -I\"${ROOT}/src\" -I\"${ROOT}/vendor/quickjs\""
  WIN_HYBRID_FLAGS="${WIN_HYBRID_FLAGS} -I\"${LIBUV_WIN_DIR}/include\""
  echo "Building Windows hybrid archive (UV + QuickJS) …"
  compile_one "${ROOT}/src/runtime/jsc_node_io.cpp"        "${STAGE_HYB_WIN}" "${WIN_HYBRID_FLAGS}"
  compile_one "${ROOT}/src/runtime/jsc_event_loop.cpp"      "${STAGE_HYB_WIN}" "${WIN_HYBRID_FLAGS}"
  compile_one "${ROOT}/src/runtime/jsc_blue_qjs.cpp"        "${STAGE_HYB_WIN}" "${WIN_HYBRID_FLAGS}"
  compile_one "${ROOT}/src/runtime/jsc_blue_plugin.cpp"     "${STAGE_HYB_WIN}" "${WIN_HYBRID_FLAGS}"
  compile_one "${ROOT}/src/runtime/jsc_qjs_node_bridge.cpp" "${STAGE_HYB_WIN}" "${WIN_HYBRID_FLAGS}"
  compile_one "${ROOT}/src/runtime/jsc_hybrid_runtime.cpp"  "${STAGE_HYB_WIN}" "${WIN_HYBRID_FLAGS}"
  rm -f "${OUT_HYBRID_WIN}"
  "${AR}" rcs "${OUT_HYBRID_WIN}" "${STAGE_HYB_WIN}"/*.o
  echo "Wrote ${OUT_HYBRID_WIN} ($(du -sh "${OUT_HYBRID_WIN}" | cut -f1))"
  rm -rf "${STAGE_HYB_WIN}"

  trap - EXIT; cleanup; exit 0
fi

# ── Hybrid archive (Linux only) ───────────────────────────────────────────────
echo "Building hybrid archive (UV + QuickJS) …"
compile_one "${ROOT}/src/runtime/jsc_node_io.cpp"        "${STAGE_HYBRID}" "${HYBRID_FLAGS}"
compile_one "${ROOT}/src/runtime/jsc_event_loop.cpp"      "${STAGE_HYBRID}" "${HYBRID_FLAGS}"
compile_one "${ROOT}/src/runtime/jsc_blue_qjs.cpp"        "${STAGE_HYBRID}" "${HYBRID_FLAGS}"
compile_one "${ROOT}/src/runtime/jsc_blue_plugin.cpp"     "${STAGE_HYBRID}" "${HYBRID_FLAGS}"
compile_one "${ROOT}/src/runtime/jsc_qjs_node_bridge.cpp" "${STAGE_HYBRID}" "${HYBRID_FLAGS}"
compile_one "${ROOT}/src/runtime/jsc_hybrid_runtime.cpp"  "${STAGE_HYBRID}" "${HYBRID_FLAGS}"

rm -f "${OUT_HYBRID}"
"${AR}" rcs "${OUT_HYBRID}" "${STAGE_HYBRID}"/*.o
echo "Wrote ${OUT_HYBRID} ($(du -sh "${OUT_HYBRID}" | cut -f1))"

# ── WebView archive (Linux only) ──────────────────────────────────────────────
echo "Building webview archive (GTK/WebKit real implementations) …"

WV_FLAGS="${BASE_FLAGS}"
if pkg-config --exists gtk+-3.0 2>/dev/null; then
  WV_FLAGS="${WV_FLAGS} $(pkg-config --cflags gtk+-3.0 2>/dev/null)"
fi
WKPKG="webkit2gtk-4.1"
pkg-config --exists webkit2gtk-4.1 2>/dev/null || WKPKG="webkit2gtk-4.0"
if pkg-config --exists "${WKPKG}" 2>/dev/null; then
  WV_FLAGS="${WV_FLAGS} $(pkg-config --cflags "${WKPKG}" 2>/dev/null)"
fi

compile_one "${ROOT}/src/runtime/linux/jsc_webview_linux.cpp"       "${STAGE_WV}" "${WV_FLAGS}"
compile_one "${ROOT}/src/runtime/linux/jsc_blue_native_linux.cpp"   "${STAGE_WV}" "${WV_FLAGS}"

rm -f "${OUT_WEBVIEW}"
"${AR}" rcs "${OUT_WEBVIEW}" "${STAGE_WV}"/*.o
echo "Wrote ${OUT_WEBVIEW} ($(du -sh "${OUT_WEBVIEW}" | cut -f1))"

trap - EXIT
cleanup
