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
SRCS  := $(shell find src -name '*.c')
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
