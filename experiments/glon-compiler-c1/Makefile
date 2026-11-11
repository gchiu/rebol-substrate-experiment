# experiments/glon-compiler-c1/Makefile
ROOT := ../..
B3 := ../glon-direct-b3
CC ?= cc
CFLAGS := -std=c17 -O2 -Wall -Wextra -I. -I$(ROOT) -I$(B3)

all: build/c1-count build/c1-bench

build:
	@mkdir -p build

build/c1-count: tools/c1_kernel.c c1_compiler.c c1_compiler.h $(B3)/b3_machine.c $(B3)/b3_machine.h bench_util.h | build
	$(CC) $(CFLAGS) -DB3_COUNT -o $@ tools/c1_kernel.c c1_compiler.c $(B3)/b3_machine.c

build/c1-bench: tools/c1_kernel.c c1_compiler.c c1_compiler.h $(B3)/b3_machine.c $(B3)/b3_machine.h bench_util.h | build
	$(CC) $(CFLAGS) -o $@ tools/c1_kernel.c c1_compiler.c $(B3)/b3_machine.c

clean:
	rm -rf build

.PHONY: all clean

test: all
	./build/c1-count --measure candidate
	./build/c1-count --measure none
	python3 tools/run_c1.py
	python3 tools/make_reference.py

.PHONY: test
