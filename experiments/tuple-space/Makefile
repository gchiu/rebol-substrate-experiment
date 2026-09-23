# experiments/tuple-space/Makefile - isolated native/ELF build for the Glon
# tuple-space experiment.  It compiles its OWN copy of the runtime with the
# experiment task-count override, so the repository's normal `s1` test binary
# and the shared m1_layout.h defaults are untouched.
#
#   make            build ./tuple-space-v003
#   make test       build and run it (invokes the external driver)
#   make clean

ROOT    := ../..
CC      ?= cc
CFLAGS  ?= -std=c17 -Wall -Wextra -O0 -g
# v0.03 needs 5 concurrent tasks; keep the loader heap [40000,54800) unchanged
# by shrinking the per-task arena to fit 5 tasks below the task table.
TSFLAGS := -DM1_MAX_TASKS=5 -DM1_TASK_CELLS=2040 -DM1_RS_OFF=2040

tuple-space-v003: tuple-space-v003-driver.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c
	$(CC) $(CFLAGS) $(TSFLAGS) -I$(ROOT) -o $@ \
	    tuple-space-v003-driver.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c

test: tuple-space-v003
	./tuple-space-v003 tuple-space-v003.glon tuple-space-v003-tests.glon

clean:
	rm -f tuple-space-v003

.PHONY: test clean
