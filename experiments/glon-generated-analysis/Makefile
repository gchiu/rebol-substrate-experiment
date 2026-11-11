# experiments/glon-generated-analysis/Makefile
ROOT := ../..
B3   := ../glon-direct-b3
B4   := ../glon-source-b4
CC   ?= cc
CFLAGS := -std=c17 -O2 -Wall -Wextra -I$(ROOT) -I$(B3) -I$(B4)

all: build/measure-count build/measure-bench

build:
	@mkdir -p build

build/measure-count: tools/measure.c $(B3)/b3_compiler.c $(B3)/b3_machine.c \
                     $(B4)/b4_tagged.c $(B3)/b3_compiler.h $(B3)/b3_machine.h \
                     $(B4)/b4_tagged.h $(B4)/b4_common.h bench_util.h | build
	$(CC) $(CFLAGS) -DB3_COUNT -o $@ tools/measure.c \
	  $(B3)/b3_compiler.c $(B3)/b3_machine.c $(B4)/b4_tagged.c

build/measure-bench: tools/measure.c $(B3)/b3_compiler.c $(B3)/b3_machine.c \
                     $(B4)/b4_tagged.c $(B3)/b3_compiler.h $(B3)/b3_machine.h \
                     $(B4)/b4_tagged.h $(B4)/b4_common.h bench_util.h | build
	$(CC) $(CFLAGS) -o $@ tools/measure.c \
	  $(B3)/b3_compiler.c $(B3)/b3_machine.c $(B4)/b4_tagged.c

clean:
	rm -rf build

.PHONY: all clean
