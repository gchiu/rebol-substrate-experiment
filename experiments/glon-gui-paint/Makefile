ROOT := ../..
CC ?= cc
CXX ?= c++
PKG_CONFIG ?= pkg-config
CFLAGS ?= -std=c17 -O2 -Wall -Wextra -I$(ROOT)
LDFLAGS ?=
SDL_CFLAGS ?= -I/usr/include/SDL2 -D_REENTRANT
SDL_LIBS ?= -lSDL2
SDL_IMAGE_CFLAGS ?=
SDL_IMAGE_LIBS ?= -lSDL2_image
PNG_CFLAGS ?=
PNG_LIBS ?= -lpng
BUILD := build
TARGET := $(BUILD)/glon-paint

NFD_AVAILABLE := $(shell $(PKG_CONFIG) --exists gtk+-3.0 && echo 1 || echo 0)
ifeq ($(NFD_AVAILABLE),1)
NFD_DEFINE := -DHAVE_NFD
NFD_CXXFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
NFD_LIBS := $(shell $(PKG_CONFIG) --libs gtk+-3.0)
NFD_OBJ := $(BUILD)/nfd_gtk.o
else
NFD_DEFINE :=
NFD_CXXFLAGS :=
NFD_LIBS :=
NFD_OBJ :=
$(warning Native file dialogs are disabled: install libgtk-3-dev and rebuild for the graphical requester. Using the Nuklear path-entry fallback.)
endif

LIBDECOR_CAIRO_AVAILABLE := $(shell $(PKG_CONFIG) --exists libdecor-0 cairo pangocairo wayland-cursor && echo 1 || echo 0)
ifeq ($(LIBDECOR_CAIRO_AVAILABLE),1)
LIBDECOR_PLUGIN_DIR := $(BUILD)/libdecor
LIBDECOR_PLUGIN := $(LIBDECOR_PLUGIN_DIR)/libdecor-cairo.so
LIBDECOR_CFLAGS := $(shell $(PKG_CONFIG) --cflags libdecor-0 cairo pangocairo wayland-cursor)
LIBDECOR_LIBS := $(shell $(PKG_CONFIG) --libs libdecor-0 cairo pangocairo wayland-cursor) -lm
LIBDECOR_SRC := third_party/libdecor-cairo/libdecor-cairo.c \
	third_party/libdecor-cairo/libdecor-cairo-blur.c \
	third_party/libdecor-cairo/cursor-settings.c
else
LIBDECOR_PLUGIN :=
LIBDECOR_CFLAGS :=
LIBDECOR_LIBS :=
LIBDECOR_SRC :=
$(warning Patched WSLg libdecor plugin is disabled: install libdecor-0-dev libcairo2-dev libpango1.0-dev libwayland-dev. Window decorations will fall back to undecorated under WSLg.)
endif

all: $(TARGET) $(LIBDECOR_PLUGIN)
$(BUILD):
	@mkdir -p $@
ifeq ($(LIBDECOR_CAIRO_AVAILABLE),1)
$(LIBDECOR_PLUGIN): $(LIBDECOR_SRC) third_party/libdecor-cairo/libdecor-plugin.h \
	third_party/libdecor-cairo/libdecor-cairo-blur.h \
	third_party/libdecor-cairo/cursor-settings.h \
	third_party/libdecor-cairo/utils.h \
	third_party/libdecor-cairo/config.h | $(BUILD)
	@mkdir -p $(LIBDECOR_PLUGIN_DIR)
	$(CC) -std=c17 -D_GNU_SOURCE -O2 -fPIC -shared -Ithird_party/libdecor-cairo \
	  $(LIBDECOR_CFLAGS) -o $@ $(LIBDECOR_SRC) $(LIBDECOR_LIBS)
endif
ifeq ($(NFD_AVAILABLE),1)
$(NFD_OBJ): third_party/nfd/nfd_gtk.cpp third_party/nfd/nfd_linux_shared.hpp \
	third_party/nfd/nfd.h | $(BUILD)
	$(CXX) -std=c++17 -O2 -Ithird_party/nfd $(NFD_CXXFLAGS) -c -o $@ $<
endif

$(TARGET): paint-host.c third_party/nuklear.h third_party/nuklear_sdl_renderer.h \
	third_party/nfd/nfd.h $(NFD_OBJ) \
	$(ROOT)/r0_s1_runtime.c $(ROOT)/r0_s1_session.c $(ROOT)/s1.c | $(BUILD)
	$(CC) $(CFLAGS) $(NFD_DEFINE) $(SDL_CFLAGS) $(SDL_IMAGE_CFLAGS) $(PNG_CFLAGS) -o $@ paint-host.c \
	  $(NFD_OBJ) $(ROOT)/r0_s1_runtime.c $(ROOT)/r0_s1_session.c $(ROOT)/s1.c \
	  $(LDFLAGS) $(SDL_LIBS) $(SDL_IMAGE_LIBS) $(NFD_LIBS) $(PNG_LIBS) -lstdc++ -lm -lpthread -ldl

test: all
	$(TARGET) --cursor-test
	$(TARGET) --self-test
	$(TARGET) --ui-test
	python3 tests/check-png.py glon-paint.png
	$(TARGET) --undo-test
	$(TARGET) --image-test
	python3 tests/check-png.py glon-paint.png
	$(TARGET) --script-test
	python3 tests/check-png.py glon-paint.png
	$(TARGET) --calligraphy-test
	python3 tests/check-png.py glon-paint.png
	$(TARGET) --replay-test
	python3 tests/check-png.py glon-paint.png
	$(TARGET) --smooth-test
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
