#
# Tony Givargis
# Copyright (C), 2023-2025
# University of California, Irvine
#
# CS 238P - Operating Systems
# Makefile
#

CC     = gcc
CFLAGS = -std=c11 -pedantic -Wall -Wextra -Werror -O2 -ffp-contract=off -D_GNU_SOURCE -MMD -MP -Isrc -Iinclude
LDLIBS = -lm -ldl
BUILD  = build
BIN    = $(BUILD)/jit
# src/web is the Emscripten build only (make wasm)
SRCS  := $(filter-out src/web/%,$(shell find src -name '*.c'))
OBJS  := $(SRCS:%.c=$(BUILD)/%.o)

all: $(BIN)

$(BIN): $(OBJS)
	@echo "[LN]" $@
	@mkdir -p $(dir $@)
	@$(CC) -o $@ $(OBJS) $(LDLIBS)

$(BUILD)/%.o: %.c
	@echo "[CC]" $<
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -rf $(BUILD)

-include $(OBJS:.o=.d)

TEST_SRCS := $(wildcard tests/unit/*.c)
TEST_BINS := $(TEST_SRCS:tests/unit/%.c=$(BUILD)/tests/%)
LIB_OBJS  := $(filter-out $(BUILD)/src/cli/main.o,$(OBJS))

test-bin: $(TEST_BINS)

$(BUILD)/tests/%: tests/unit/%.c tests/check.h $(LIB_OBJS)
	@echo "[CC]" $<
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $< $(LIB_OBJS) -o $@ $(LDLIBS)

DIFFFUZZ_BIN := $(BUILD)/tests/difffuzz

$(DIFFFUZZ_BIN): tests/fuzz/difffuzz.c tests/fuzz/gen.c tests/fuzz/gen.h $(LIB_OBJS)
	@echo "[CC]" tests/fuzz/difffuzz.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) tests/fuzz/difffuzz.c tests/fuzz/gen.c $(LIB_OBJS) -o $@ $(LDLIBS)

DIFFTEST_BIN := $(BUILD)/difftest
RULEFUZZ_BIN := $(BUILD)/rulefuzz

$(DIFFTEST_BIN): tests/fuzz/difftest.c tests/fuzz/gen.c tests/fuzz/gen.h $(LIB_OBJS)
	@echo "[CC]" tests/fuzz/difftest.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) tests/fuzz/difftest.c tests/fuzz/gen.c $(LIB_OBJS) -o $@ $(LDLIBS)

$(RULEFUZZ_BIN): tests/fuzz/rulefuzz.c tests/fuzz/gen.c tests/fuzz/gen.h $(LIB_OBJS)
	@echo "[CC]" tests/fuzz/rulefuzz.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) tests/fuzz/rulefuzz.c tests/fuzz/gen.c $(LIB_OBJS) -o $@ $(LDLIBS)

# test_encode writes build/enc.bin + build/enc.lst for check-encoding.sh
check: all test-bin $(DIFFFUZZ_BIN) $(DIFFTEST_BIN) $(RULEFUZZ_BIN)
	@set -e; for t in $(TEST_BINS); do $$t; done
	@bash tests/check-encoding.sh
	@bash tests/check-repl.sh
	@$(DIFFFUZZ_BIN)
	@$(DIFFTEST_BIN) --n 5000 --gcc-every 250
	@$(RULEFUZZ_BIN) --n 100000
	@tests/run-golden.sh

# long fuzz run (CI job "fuzz")
fuzz: $(DIFFTEST_BIN) $(RULEFUZZ_BIN)
	@$(DIFFTEST_BIN) --n 100000 --gcc-every 50
	@$(RULEFUZZ_BIN) --n 1000000

# AddressSanitizer + UBSan build in build-asan/. The x86 JIT code itself is not
# instrumented (only the C around it). The gcc backend stays on: its dlopen'd
# .so is plain C, so it needs no sanitizer runtime.
ASAN_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer -O1
# gcc-9 libsanitizer crashes (DEADLYSIGNAL) on a random share of runs with the
# address-space randomisation of recent kernels (seen on WSL2, 6.x), so the asan
# runs switch ASLR off when setarch can; the personality is inherited by children.
ASAN_RUN := $(shell setarch $$(uname -m) -R true 2>/dev/null && echo setarch $$(uname -m) -R)
asan:
	@mkdir -p build
	@$(MAKE) --no-print-directory BUILD=build-asan \
		CFLAGS='-std=c11 -pedantic -Wall -Wextra -Werror $(ASAN_FLAGS) -ffp-contract=off -D_GNU_SOURCE -MMD -MP -Isrc -Iinclude' \
		LDLIBS='-lm -ldl $(ASAN_FLAGS)' \
		build-asan/jit test-bin build-asan/difftest
	@set -e; for t in $(TEST_BINS:$(BUILD)/%=build-asan/%); do $(ASAN_RUN) $$t; done
	@$(ASAN_RUN) build-asan/difftest --n 2000 --gcc-every 250
	@$(ASAN_RUN) env JIT=build-asan/jit tests/run-golden.sh

.PHONY: all clean test-bin check fuzz asan

bench: all
	@bash bench/run.sh

.PHONY: bench

# WebAssembly build of the whole engine (web/dist/engine.mjs + engine.wasm).
# emcc runs in the pinned emscripten/emsdk image (docker CLI; works from WSL).
# Inside that image emcc is on the PATH and is used directly. From PowerShell:
#   docker run --rm -v "${PWD}:/src" -w /src emscripten/emsdk:3.1.74 make wasm
EMSDK_TAG  = 3.1.74
EMCC      ?= $(if $(shell command -v emcc 2>/dev/null),emcc,docker run --rm -v "$(CURDIR)":/src -w /src emscripten/emsdk:$(EMSDK_TAG) emcc)
WASM_SRCS := $(filter-out src/cli/main.c src/backend/gcc/%,$(shell find src -name '*.c'))
WASM_EXPORTS = _jitw_load,_jitw_error_json,_jitw_stage,_jitw_nvars,_jitw_var_name,_jitw_wasm,_jitw_wasm_len,_jitw_set_var,_jitw_interp,_jitw_interp_sum,_jitw_free,_malloc,_free

wasm:
	@mkdir -p web/dist
	$(EMCC) -std=c11 -Wall -Wextra -O2 -ffp-contract=off -D_GNU_SOURCE -Isrc -Iinclude \
		$(WASM_SRCS) -o web/dist/engine.mjs --no-entry \
		-sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1 \
		-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,HEAPU8,HEAPF64 -sFILESYSTEM=0 \
		-sEXPORTED_FUNCTIONS=$(WASM_EXPORTS)

.PHONY: wasm

# The folder that ships: the page, fonts, shared imports and the make wasm outputs.
web: web/dist/engine.mjs web/dist/engine.wasm
	@rm -rf web/site
	@mkdir -p web/site
	@cp web/index.html web/app.js web/style.css web/imports.mjs web/logo.svg web/site/
	@cp -r web/fonts web/site/fonts
	@cp web/dist/engine.mjs web/dist/engine.wasm web/site/
	@du -sh web/site

.PHONY: web
