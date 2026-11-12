ROOT := ../..
CC ?= cc
CFLAGS ?= -std=c17 -O2 -Wall -Wextra -I$(ROOT)
SDL_CFLAGS ?= -I/usr/include/SDL2 -D_REENTRANT
SDL_LIBS ?= -lSDL2
PNG_CFLAGS ?=
PNG_LIBS ?= -lpng
BUILD := build
TARGET := $(BUILD)/glon-paint

all: $(TARGET)
$(BUILD):
	@mkdir -p $@
$(TARGET): paint-host.c $(ROOT)/r0_s1_runtime.c $(ROOT)/r0_s1_session.c $(ROOT)/s1.c | $(BUILD)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(PNG_CFLAGS) -o $@ paint-host.c \
	  $(ROOT)/r0_s1_runtime.c $(ROOT)/r0_s1_session.c $(ROOT)/s1.c \
	  $(SDL_LIBS) $(PNG_LIBS) -lm -lpthread

test: all
	$(TARGET) --self-test
	$(TARGET) --move-stress 10000
	python3 tests/check-png.py glon-paint.png

clean:
	rm -rf $(BUILD)
.PHONY: all test clean
