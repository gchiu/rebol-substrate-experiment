# experiments/glon-execution-b8/Makefile
ROOT := ../..
B3   := ../glon-direct-b3
CC   ?= cc
CFLAGS := -std=c17 -O2 -Wall -Wextra -I$(ROOT) -I$(B3)

all: build/bench-count build/bench-fast

build:
	@mkdir -p build

build/bench-count: tools/bench.c $(B3)/b3_compiler.c $(B3)/b3_machine.c \
                   $(B3)/b3_compiler.h $(B3)/b3_machine.h bench_util.h | build
	$(CC) $(CFLAGS) -DB3_COUNT -o $@ tools/bench.c $(B3)/b3_compiler.c $(B3)/b3_machine.c

build/bench-fast: tools/bench.c $(B3)/b3_compiler.c $(B3)/b3_machine.c \
                  $(B3)/b3_compiler.h $(B3)/b3_machine.h bench_util.h | build
	$(CC) $(CFLAGS) -o $@ tools/bench.c $(B3)/b3_compiler.c $(B3)/b3_machine.c

clean:
	rm -rf build

.PHONY: all clean
