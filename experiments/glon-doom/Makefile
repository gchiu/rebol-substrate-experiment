ROOT := ../..
CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -std=c17 -O2 -Wall -Wextra -I$(ROOT)
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null || echo -I/usr/include/SDL2)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null || echo -lSDL2)
BUILD := build
TARGET := $(BUILD)/doom
WIN_BUILD := build-win
WIN_TARGET := $(WIN_BUILD)/doom.exe
WIN_ROOT := ../..
WIN_SDL := ../../.deps/SDL2-2.30.9/x86_64-w64-mingw32
WIN_CC ?= /home/graham/.cache/glon-g6d/zig-linux-x86_64-0.13.0/zig cc -target x86_64-windows-gnu
WIN_CFLAGS := -std=c17 -O2 -Wall -Wextra -I$(WIN_ROOT) -DSDL_MAIN_HANDLED -I$(WIN_SDL)/include
WIN_LIBS := -L$(WIN_SDL)/lib $(WIN_SDL)/lib/libSDL2.dll.a -lopengl32 -lwinmm -lole32 -loleaut32 -luuid -limm32 -lversion
SRCS := doom-host.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
OPCODE_TARGET := $(BUILD)/doom-opcode
all: $(TARGET)

$(WIN_TARGET): doom-host.c textures.h weapon_sprites.h alien_sprites_g5s.h $(WIN_ROOT)/r0_s1.h $(WIN_ROOT)/m1_layout.h $(WIN_ROOT)/s1.h | $(WIN_BUILD)
	$(WIN_CC) $(WIN_CFLAGS) -o $@ doom-host.c $(WIN_ROOT)/r0_s1_session.c $(WIN_ROOT)/r0_s1_runtime.c $(WIN_ROOT)/s1.c $(WIN_LIBS)

windows: $(WIN_TARGET)
$(BUILD):
	@mkdir -p $@

$(WIN_BUILD):
	@mkdir -p $@
$(TARGET): doom-host.c textures.h weapon_sprites.h alien_sprites_g5s.h $(ROOT)/r0_s1.h $(ROOT)/m1_layout.h $(ROOT)/s1.h | $(BUILD)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $(SRCS) $(SDL_LIBS) -lGL -lm -lpthread

# Diagnostic build: instrumented COPY of frozen S1 (s1_prof.c), for event counts only.
# The production binary above still links the frozen s1.c. This target is not
# part of the game default and changes no production semantics.
$(OPCODE_TARGET): doom-host.c textures.h weapon_sprites.h alien_sprites_g5s.h $(ROOT)/r0_s1.h $(ROOT)/m1_layout.h $(ROOT)/s1.h $(ROOT)/s1_prof.h $(ROOT)/s1_prof.c | $(BUILD)
	$(CC) $(CFLAGS) -DDOOM_S1_PROFILE $(SDL_CFLAGS) -o $@ doom-host.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1_prof.c $(SDL_LIBS) -lm -lpthread

bench-tools:
	$(MAKE) -C bench

diagnostics: $(OPCODE_TARGET) bench-tools

test: $(TARGET)
	python3 tests/check-textures.py
	python3 tests/check_aliens.py
	python3 tests/check_g5s_alien.py
	python3 tests/check_g5j_textures.py
	python3 tests/check_g5x_door.py
	python3 tests/check_g5x2_door.py
	python3 tests/check_g6a_audio.py
	python3 tests/check_g6b_audio.py
	./$(TARGET) --self-test
	./$(TARGET) --projection-test
	./$(TARGET) --input-test
	./$(TARGET) --tracker-test
	./$(TARGET) --resize-test
	./$(TARGET) --stationary-test
	./$(TARGET) --flat-surfaces --objects-test
	./$(TARGET) --self-test --raw-wall
	./$(TARGET) --trace
	./$(TARGET) --audio-test
	./$(TARGET) --sfx-test
	./$(TARGET) --audio-e2e
	cd $(ROOT) && ./check-frozen-s1.sh

run: $(TARGET)
	DOOM_ROOT=$(ROOT) ./$(TARGET)

bench: $(TARGET)
	DOOM_ROOT=$(ROOT) ./$(TARGET) --bench 60

clean:
	rm -rf $(BUILD)
.PHONY: all windows test run bench bench-tools diagnostics clean
