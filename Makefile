#
# Tony Givargis
# Copyright (C), 2023-2025
# University of California, Irvine
#
# CS 238P - Operating Systems
# Makefile
#

CC     = gcc
CFLAGS = -std=c11 -pedantic -Wall -Wextra -Werror -O2 -D_GNU_SOURCE -MMD -MP -Isrc
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

.PHONY: all clean
