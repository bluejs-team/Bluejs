CC ?= cc
BLUE_ROOT ?= ../..
RAYLIB_PREFIX ?= .raylib/install
BLUE_ROOT_ABS := $(abspath $(BLUE_ROOT))
RAYLIB_PC_DIR := $(abspath $(RAYLIB_PREFIX))/lib/pkgconfig
RAYLIB_LIB_DIR := $(abspath $(RAYLIB_PREFIX))/lib
PLUGIN := build/blue_raylib.so

.PHONY: all clean install-raylib run

all: $(PLUGIN)

install-raylib:
	./scripts/install-raylib.sh

$(PLUGIN): plugin/blue_raylib.c $(BLUE_ROOT)/src/runtime/blue_plugin.h
	@PKG_CONFIG_PATH="$(RAYLIB_PC_DIR):$${PKG_CONFIG_PATH:-}" pkg-config --exists raylib || { echo "raylib not found: run 'make -C examples/raylib-plugin install-raylib' or install raylib system-wide"; exit 1; }
	@mkdir -p build
	$(CC) -shared -fPIC -I$(BLUE_ROOT)/src plugin/blue_raylib.c -o $(PLUGIN) $$(PKG_CONFIG_PATH="$(RAYLIB_PC_DIR):$${PKG_CONFIG_PATH:-}" pkg-config --cflags --libs raylib) -Wl,-rpath,$(RAYLIB_LIB_DIR)

run: all
	cd $(BLUE_ROOT_ABS) && LD_LIBRARY_PATH="$(RAYLIB_LIB_DIR):$${LD_LIBRARY_PATH:-}" ./blue -build examples/raylib-plugin -o /tmp/blue_raylib_demo
	cd $(BLUE_ROOT_ABS) && LD_LIBRARY_PATH="$(RAYLIB_LIB_DIR):$${LD_LIBRARY_PATH:-}" /tmp/blue_raylib_demo

clean:
	rm -rf build
