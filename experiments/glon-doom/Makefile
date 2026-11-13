ROOT := ../..
CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -std=c17 -O2 -Wall -Wextra -I$(ROOT)
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null || echo -I/usr/include/SDL2)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null || echo -lSDL2)
BUILD := build
TARGET := $(BUILD)/doom
SRCS := doom-host.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
TEXHDR := $(BUILD)/textures.h

all: $(TARGET)
$(BUILD):
	@mkdir -p $@
$(TEXHDR): tools/gen_textures.py | $(BUILD)
	python3 tools/gen_textures.py $@
$(TARGET): doom-host.c $(ROOT)/r0_s1.h $(ROOT)/m1_layout.h $(ROOT)/s1.h $(TEXHDR) | $(BUILD)
	$(CC) $(CFLAGS) -I$(BUILD) $(SDL_CFLAGS) -o $@ $(SRCS) $(SDL_LIBS) -lm -lpthread

test: $(TARGET)
	./$(TARGET) --self-test
	./$(TARGET) --trace
	cd $(ROOT) && ./check-frozen-s1.sh

run: $(TARGET)
	DOOM_ROOT=$(ROOT) ./$(TARGET)

bench: $(TARGET)
	DOOM_ROOT=$(ROOT) ./$(TARGET) --bench 60

clean:
	rm -rf $(BUILD)
.PHONY: all test run bench clean
