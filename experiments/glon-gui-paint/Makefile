ROOT := ../..
CC ?= cc
CFLAGS ?= -std=c17 -O2 -Wall -Wextra -I$(ROOT)
SDL_CFLAGS ?= -I/usr/include/SDL2 -D_REENTRANT
SDL_LIBS ?= -lSDL2
SDL_IMAGE_CFLAGS ?=
SDL_IMAGE_LIBS ?= -lSDL2_image
PNG_CFLAGS ?=
PNG_LIBS ?= -lpng
BUILD := build
TARGET := $(BUILD)/glon-paint

all: $(TARGET)
$(BUILD):
	@mkdir -p $@
$(TARGET): paint-host.c third_party/nuklear.h third_party/nuklear_sdl_renderer.h \
	$(ROOT)/r0_s1_runtime.c $(ROOT)/r0_s1_session.c $(ROOT)/s1.c | $(BUILD)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(SDL_IMAGE_CFLAGS) $(PNG_CFLAGS) -o $@ paint-host.c \
	  $(ROOT)/r0_s1_runtime.c $(ROOT)/r0_s1_session.c $(ROOT)/s1.c \
	  $(SDL_LIBS) $(SDL_IMAGE_LIBS) $(PNG_LIBS) -lm -lpthread

test: all
	$(TARGET) --self-test
	$(TARGET) --ui-test
	python3 tests/check-png.py glon-paint.png
	$(TARGET) --undo-test
	$(TARGET) --image-test
	python3 tests/check-png.py glon-paint.png
	$(TARGET) --coalesce-test
	python3 tests/check-png.py glon-paint.png
	$(TARGET) --resize-test
	python3 tests/check-png.py glon-paint.png
	$(TARGET) --move-stress 10000
	python3 tests/check-png.py glon-paint.png

clean:
	rm -rf $(BUILD)
.PHONY: all test clean
