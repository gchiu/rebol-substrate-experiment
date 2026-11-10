# experiments/glon-direct-b1/Makefile
#
# B1: direct execution of a small compiled Glon subset.
# The frozen S1 machine is linked unchanged for the tests;
# s1_count.c is a measurement copy with counters only.
#
#   make test       compile/run positive and negative subset tests (frozen s1.c)
#   make measure    compiled fib, Glon evaluator and hand-written masm fib
#   make all        both binaries
#   make clean

ROOT  := ../..
CC    ?= cc
BUILD := build

CFLAGS      := -std=c17 -O2 -Wall -Wextra -I$(ROOT)
COUNTFLAGS  := -DB1_COUNT -I$(ROOT)

RUNTIME     := $(ROOT)/r0_s1_runtime.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c

all: $(BUILD)

$(BUILD):
	@mkdir -p $@

# Tests run on the frozen s1.c exactly as shipped.
$(BUILD)/b1-test: b1_main.c b1_compiler.c b1_compiler.h $(ROOT)/s1.c $(ROOT)/s1.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ b1_main.c b1_compiler.c $(ROOT)/s1.c

# Measurements use s1_count.c (same semantics, counters added).
$(BUILD)/b1-measure: b1_main.c b1_compiler.c b1_compiler.h s1_count.c s1_count.h \
                     $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) $(COUNTFLAGS) -o $@ b1_main.c b1_compiler.c s1_count.c

# Baselines use the repository's production runtime and the counter copy.
$(BUILD)/baseline: baseline.c s1_count.c s1_count.h $(RUNTIME) \
                   $(ROOT)/r0_s1.h $(ROOT)/s1.h $(ROOT)/m1_layout.h | $(BUILD)
	$(CC) $(CFLAGS) $(COUNTFLAGS) -o $@ baseline.c s1_count.c $(RUNTIME)

$(BUILD)/b1_compiler.o: b1_compiler.c b1_compiler.h $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ b1_compiler.c

size: $(BUILD)/b1_compiler.o
	@echo "compiler source lines:"
	@wc -l b1_compiler.c b1_compiler.h
	@echo "compiler object size:"
	@size $(BUILD)/b1_compiler.o

test: $(BUILD)/b1-test
	cd $(ROOT) && ./check-frozen-s1.sh
	./$(BUILD)/b1-test

measure: $(BUILD)/b1-measure $(BUILD)/baseline
	./$(BUILD)/b1-measure --measure
	./$(BUILD)/baseline $(ROOT)

clean:
	rm -rf $(BUILD)

.PHONY: all test measure size clean
