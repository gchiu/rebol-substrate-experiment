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
endif

all: $(TARGET)
$(BUILD):
	@mkdir -p $@
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
	  $(LDFLAGS) $(SDL_LIBS) $(SDL_IMAGE_LIBS) $(NFD_LIBS) $(PNG_LIBS) -lstdc++ -lm -lpthread

test: all
	$(TARGET) --self-test
	$(TARGET) --ui-test
	python3 tests/check-png.py glon-paint.png
	$(TARGET) --undo-test
	$(TARGET) --image-test
	python3 tests/check-png.py glon-paint.png
	$(TARGET) --script-test
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
