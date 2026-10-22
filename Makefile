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

FUZZ_BIN := $(BUILD)/tests/optfuzz

$(FUZZ_BIN): tests/fuzz/optfuzz.c tests/fuzz/gen.c tests/fuzz/gen.h $(LIB_OBJS)
	@echo "[CC]" tests/fuzz/optfuzz.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) tests/fuzz/optfuzz.c tests/fuzz/gen.c $(LIB_OBJS) -o $@ $(LDLIBS)

X86FUZZ_BIN := $(BUILD)/tests/x86fuzz

$(X86FUZZ_BIN): tests/fuzz/x86fuzz.c tests/fuzz/gen.c tests/fuzz/gen.h $(LIB_OBJS)
	@echo "[CC]" tests/fuzz/x86fuzz.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) tests/fuzz/x86fuzz.c tests/fuzz/gen.c $(LIB_OBJS) -o $@ $(LDLIBS)

# test_encode writes build/enc.bin + build/enc.lst for check-encoding.sh
check: all test-bin $(FUZZ_BIN) $(X86FUZZ_BIN)
	@set -e; for t in $(TEST_BINS); do $$t; done
	@bash tests/check-encoding.sh
	@bash tests/check-repl.sh
	@$(FUZZ_BIN)
	@$(X86FUZZ_BIN)
	@tests/run-golden.sh

.PHONY: all clean test-bin check
