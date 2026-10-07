CC ?= cc
CPPFLAGS ?= -Iinclude
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic
LDFLAGS ?=

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin

SYSWATCH_SOURCES := src/main.c src/proc.c src/net.c src/util.c
SYSWATCH_OBJECTS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SYSWATCH_SOURCES))
EXAMPLE_NAMES := memory_leak zombie cpu_hog fd_leak tcp_server tcp_client tcp_failure
EXAMPLE_BINS := $(addprefix $(BIN_DIR)/,$(EXAMPLE_NAMES))

.PHONY: all clean examples check help

all: $(BIN_DIR)/syswatch examples

examples: $(EXAMPLE_BINS)

$(BIN_DIR)/syswatch: $(SYSWATCH_OBJECTS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: src/%.c include/syswatch.h | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/%: examples/%.c | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) -o $@

$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

check: all
	@echo "Running smoke checks..."
	@$(BIN_DIR)/syswatch --help >/dev/null
	@$(BIN_DIR)/syswatch --pid $$$$ >/dev/null
	@$(BIN_DIR)/syswatch --maps $$$$ >/dev/null
	@$(BIN_DIR)/syswatch --net >/dev/null
	@echo "Smoke checks passed."

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "make          Build syswatch and all diagnostic fixtures"
	@echo "make check    Build and run non-destructive smoke checks"
	@echo "make clean    Remove generated build files"
