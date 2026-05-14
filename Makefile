CXX      := g++
VERSION  := 1.0.0
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Isrc -Ivendor/quickjs -DBLUE_VERSION=\"$(VERSION)\"
CC_HOST  := cc
TARGET   := blue_bin

# Tarball name uses -2 suffix; extracted directory omits it.
QJS_TARBALL_VER := 2025-09-13-2
QJS_SRC_DIRNAME := quickjs-2025-09-13
QJS_DIR  := vendor/quickjs
QJS_URL  := https://bellard.org/quickjs/quickjs-$(QJS_TARBALL_VER).tar.xz
QJS_SRCS := quickjs.c dtoa.c libregexp.c libunicode.c cutils.c quickjs-libc.c
QJS_OBJS := $(patsubst %.c,$(QJS_DIR)/obj/%.o,$(QJS_SRCS))
NLOHMANN_JSON_VER := 3.12.0

# Match QuickJS upstream defaults (see vendor quickjs/Makefile).
QJS_CFLAGS := -O2 -fwrapv -Wall \
	-Wno-array-bounds -Wno-format-truncation -Wno-unused-parameter \
	-Wno-sign-compare -Wno-missing-field-initializers \
	-I$(QJS_DIR) -D_GNU_SOURCE -DCONFIG_VERSION=\"$(QJS_TARBALL_VER)\"

CXX_WIN  := x86_64-w64-mingw32-g++
CC_WIN   := x86_64-w64-mingw32-gcc
QJS_OBJS_WIN := $(patsubst %.c,$(QJS_DIR)/obj-win/%.o,$(QJS_SRCS))
CLI_HDRS := $(wildcard src/cli/*.hpp)
# MinGW: drop _GNU_SOURCE and format-truncation (not available on Windows)
QJS_CFLAGS_WIN := -O2 -fwrapv -Wall \
	-Wno-array-bounds -Wno-unused-parameter \
	-Wno-sign-compare -Wno-missing-field-initializers \
	-I$(QJS_DIR) -DCONFIG_VERSION=\"$(QJS_TARBALL_VER)\"

.PHONY: all deps clean test test-babel-emit tools-deps test-aot-perf test-shift-left test-examples-smoke test-qjs-node-compat test-plugin-api check check-deps format windows

check-deps:
	@command -v $(CXX) >/dev/null 2>&1 || { \
	    echo ""; \
	    echo "Error: C++ compiler '$(CXX)' not found."; \
	    echo "  Ubuntu/Debian:  sudo apt install build-essential"; \
	    echo "  Fedora:         sudo dnf groupinstall 'Development Tools'"; \
	    echo "  macOS:          xcode-select --install"; \
	    echo ""; \
	    exit 1; \
	}

all: check-deps deps tools-deps $(TARGET)

	@chmod +x blue
	@test ! -e jsc || chmod +x jsc
	@echo "Ready.  ./blue -compile examples/aot-math/main.js"

$(QJS_DIR)/obj:
	@mkdir -p $(QJS_DIR)/obj

$(QJS_DIR)/obj/%.o: $(QJS_DIR)/%.c | $(QJS_DIR)/obj
	$(CC_HOST) $(QJS_CFLAGS) -c -o $@ $<

$(TARGET): src/main.cpp $(QJS_OBJS) \
           src/ast_node.hpp src/parser.hpp src/emitter.hpp \
           src/module_resolver.hpp src/babel_transform.hpp \
           src/js_value.h src/js_value.hpp src/js_window.h $(CLI_HDRS) \
           src/jsc_npm_bundle.hpp
	$(CXX) $(CXXFLAGS) src/main.cpp $(QJS_OBJS) -o $(TARGET) -lm -lpthread -ldl
	@echo "Built $(TARGET)."

deps:
	@command -v curl >/dev/null 2>&1 || { \
	    echo "Error: 'curl' is required for make deps."; \
	    echo "  Ubuntu/Debian:  sudo apt install curl"; \
	    exit 1; \
	}
	@mkdir -p src/nlohmann vendor/js
	@if [ ! -f $(QJS_DIR)/quickjs.c ]; then \
	    echo "[fetch] QuickJS $(QJS_TARBALL_VER)..."; \
	    mkdir -p vendor && \
	    curl -fsSL '$(QJS_URL)' | tar -xJ -C vendor && \
	    rm -rf '$(QJS_DIR)' && \
	    mv 'vendor/$(QJS_SRC_DIRNAME)' '$(QJS_DIR)'; \
	    echo "[ok]"; \
	else echo "[skip] QuickJS already present."; fi
	@if [ ! -f vendor/js/esprima.js ]; then \
	    echo "[fetch] esprima.js..."; \
	    curl -fsSL https://cdn.jsdelivr.net/npm/esprima@4.0.1/dist/esprima.js -o vendor/js/esprima.js; \
	    echo "[ok]"; \
	else echo "[skip] esprima.js already present."; fi
	@if [ ! -f vendor/js/babel.min.js ]; then \
	    echo "[fetch] @babel/standalone..."; \
	    curl -fsSL https://cdn.jsdelivr.net/npm/@babel/standalone@7.26.9/babel.min.js \
	        -o vendor/js/babel.min.js; \
	    echo "[ok]"; \
	else echo "[skip] babel.min.js already present."; fi
	@if [ ! -f src/nlohmann/json.hpp ]; then \
	    echo "[fetch] nlohmann/json $(NLOHMANN_JSON_VER)..."; \
	    curl -fsSL https://github.com/nlohmann/json/releases/download/v$(NLOHMANN_JSON_VER)/json.hpp \
	        -o src/nlohmann/json.hpp; \
	    echo "[ok]"; \
	else echo "[skip] nlohmann/json already present."; fi

tools-deps:
	@command -v node >/dev/null 2>&1 || { \
	    echo "Error: Node.js is required for make tools-deps."; \
	    echo "  See: https://nodejs.org/en/download"; \
	    exit 1; \
	}
	@echo "[npm] tools/jsc-npm-bundle..."
	@cd tools/jsc-npm-bundle && npm install || echo "Warning: npm install failed for tools. NPM bundling may be unavailable."
	@echo "[ok] esbuild ready for Blue npm bundling."

test: all
	@echo "=== aot-math (AOT loops) ==="
	@./blue -compile examples/aot-math/main.js -o /tmp/blue_math_test
	@/tmp/blue_math_test > /tmp/blue_math_out.txt
	@grep -Fxq '5999995' /tmp/blue_math_out.txt
	@grep -Fxq '50005000' /tmp/blue_math_out.txt
	@echo "=== init defaults ==="
	@rm -rf /tmp/blue_init_test
	@./blue -init /tmp/blue_init_test
	@test -f /tmp/blue_init_test/src/main.js
	@test -f /tmp/blue_init_test/src/island.js
	@test -f /tmp/blue_init_test/blue.config.json
	@grep -q '"hybrid": true' /tmp/blue_init_test/blue.config.json
	@grep -q '"quickjsIsland": "src/island.js"' /tmp/blue_init_test/blue.config.json
	@if pkg-config --exists libuv 2>/dev/null; then \
	  echo "=== http-server hybrid (libuv + curl + Blue.System) ==="; \
	  fuser -k 48311/tcp 2>/dev/null || true; \
	  ./blue -build examples/http-server -o /tmp/blue_http_test; \
	  /tmp/blue_http_test & HPID=$$!; sleep 1.0; \
	  curl -sSf http://127.0.0.1:48311/ | grep -q '^ok$$'; \
	  curl -sSf http://127.0.0.1:48311/api/blue | grep -q '"memory"'; \
	  kill -9 $$HPID 2>/dev/null || true; \
	else \
	  echo "[skip] http-server - pkg-config libuv not found"; \
	fi
	@echo "=== markdown-notes-demo (build smoke) ==="
	@./blue -build examples/markdown-notes-demo -o /tmp/blue_notes_smoke
	@echo "=== hello-webview (compile smoke) ==="
	@./blue -compile examples/hello-webview/main.js -o /tmp/blue_hello_smoke
	@echo "=== pass ==="

test-examples-smoke: test

test-babel-emit: all
	@if [ -x ./tests/babel_emit/check.sh ]; then \
	  ./tests/babel_emit/check.sh; \
	else \
	  echo "[skip] test-babel-emit - tests/babel_emit/check.sh not present"; \
	fi

test-aot-perf: all
	@if [ -x ./tests/aot-perf/run_perf.sh ]; then \
	  ./tests/aot-perf/run_perf.sh; \
	else \
	  echo "[skip] test-aot-perf - tests/aot-perf/run_perf.sh not present"; \
	fi

test-shift-left: all
	@if [ -x ./tests/shift-left/run_shift_left.sh ]; then \
	  ./tests/shift-left/run_shift_left.sh; \
	else \
	  echo "[skip] test-shift-left - tests/shift-left/run_shift_left.sh not present"; \
	fi

test-qjs-node-compat: all
	@./tests/qjs-node-compat/run_qjs_node_compat.sh

test-plugin-api: all
	@./tests/plugin-api/run_plugin_api.sh

check: test test-qjs-node-compat test-plugin-api
	@bash -n build/deploy.sh build/pipeline.sh build/precompile-runtime.sh build/make-windows-installer.sh tests/windows-wine/run_wine_smoke.sh
	@! rg -n --pcre2 '\x{2014}' . -g '!node_modules' -g '!.git'

format:
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not found"; exit 1; }
	@clang-format -i src/*.cpp src/*.hpp src/*.h src/cli/*.hpp src/runtime/*.cpp src/runtime/*.h src/runtime/linux/*.cpp src/runtime/linux/*.h src/runtime/macos/*.mm src/runtime/windows/*.cpp

# Compiles golden stitch-shaped bundle (no esbuild/npm at test time).
.PHONY: test-npm-bundle
test-npm-bundle: all
	@echo "=== npm-bundle-fixture (golden stitch-shaped bundle) ==="
	@./blue -compile tests/npm-bundle-fixture/.jsc-build/main.bundled.js -o /tmp/blue_npm_bundle_test
	@/tmp/blue_npm_bundle_test | grep -q '^41$$'
	@echo "=== npm-bundle pass ==="

$(QJS_DIR)/obj-win:
	@mkdir -p $(QJS_DIR)/obj-win

$(QJS_DIR)/obj-win/%.o: $(QJS_DIR)/%.c | $(QJS_DIR)/obj-win
	$(CC_WIN) $(QJS_CFLAGS_WIN) -c -o $@ $<

windows: deps tools-deps $(QJS_OBJS_WIN)

	@command -v $(CXX_WIN) >/dev/null 2>&1 || { \
	    echo "Error: $(CXX_WIN) not found."; \
	    echo "  Ubuntu/Debian:  sudo apt install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64"; \
	    exit 1; \
	}
	$(CXX_WIN) -std=c++17 -O2 -Isrc -Ivendor/quickjs \
	    -DBLUE_VERSION=\"$(VERSION)\" \
	    src/main.cpp $(QJS_OBJS_WIN) \
	    -o blue_bin.exe \
	    -lm -static-libgcc -static-libstdc++ \
	    -Wl,-Bstatic -lpthread -Wl,-Bdynamic
	@echo "Built blue_bin.exe (Windows x86_64)."

clean:
	rm -f $(TARGET) blue_bin.exe $(QJS_DIR)/obj/*.o $(QJS_DIR)/obj-win/*.o *.out
