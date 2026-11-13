ROOT := ../..
CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -std=c17 -O2 -Wall -Wextra -I$(ROOT)
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null || echo -I/usr/include/SDL2)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null || echo -lSDL2)
BUILD := build
TARGET := $(BUILD)/doom
SRCS := doom-host.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
OPCODE_TARGET := $(BUILD)/doom-opcode
all: $(TARGET)
$(BUILD):
	@mkdir -p $@
$(TARGET): doom-host.c textures.h $(ROOT)/r0_s1.h $(ROOT)/m1_layout.h $(ROOT)/s1.h | $(BUILD)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(SRCS) $(SDL_LIBS) -lm -lpthread

# Diagnostic build: instrumented COPY of frozen S1 (s1_prof.c), for event counts only.
# The production binary above still links the frozen s1.c. This target is not
# part of the game default and changes no production semantics.
$(OPCODE_TARGET): doom-host.c textures.h $(ROOT)/r0_s1.h $(ROOT)/m1_layout.h $(ROOT)/s1.h $(ROOT)/s1_prof.h $(ROOT)/s1_prof.c | $(BUILD)
	$(CC) $(CFLAGS) -DDOOM_S1_PROFILE $(SDL_CFLAGS) -o $@ doom-host.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1_prof.c $(SDL_LIBS) -lm -lpthread

bench-tools:
	$(MAKE) -C bench

diagnostics: $(OPCODE_TARGET) bench-tools

test: $(TARGET)
	python3 tests/check-textures.py
	python3 tests/check_aliens.py
	python3 tests/check_g5j_textures.py
	./$(TARGET) --self-test
	./$(TARGET) --projection-test
	./$(TARGET) --stationary-test
	./$(TARGET) --self-test --raw-wall
	./$(TARGET) --trace
	cd $(ROOT) && ./check-frozen-s1.sh

run: $(TARGET)
	DOOM_ROOT=$(ROOT) ./$(TARGET)

bench: $(TARGET)
	DOOM_ROOT=$(ROOT) ./$(TARGET) --bench 60

clean:
	rm -rf $(BUILD)
.PHONY: all test run bench bench-tools diagnostics clean
