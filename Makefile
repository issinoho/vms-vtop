# Linux build of the core and its tests, and a demo on the stub platform.
# The VMS build is build_vms.com.
CC ?= cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Werror -g -Isrc/core -Isrc/plat
CORE = src/core/vp_util.c src/core/vp_screen.c src/core/vp_theme.c src/core/vp_term.c \
       src/core/vp_model.c src/core/vp_graph.c src/core/vp_procs.c src/core/vp_vms.c \
       src/core/vp_opts.c src/core/vp_app.c
HDRS = $(wildcard src/core/*.h src/plat/*.h)

all: test

build/test_core: tests/unit/test_core.c $(CORE) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/unit/test_core.c $(CORE)

build/test_app: tests/unit/test_app.c $(CORE) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/unit/test_app.c $(CORE)

build/vtop_demo: src/main/vp_main.c src/plat/stub/vp_plat_stub.c $(CORE) $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/main/vp_main.c src/plat/stub/vp_plat_stub.c $(CORE) -lm

test: build/test_core build/test_app build/vtop_demo
	./build/test_core
	./build/test_app

snapshots: build/test_app
	VP_UPDATE_SNAPSHOTS=1 ./build/test_app

demo: build/vtop_demo
	./build/vtop_demo

clean:
	rm -rf build
.PHONY: all test snapshots demo clean
