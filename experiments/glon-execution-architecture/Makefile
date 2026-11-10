# experiments/glon-execution-architecture/Makefile
#
# Builds the measurement driver used by GLON-EXECUTION-ARCHITECTURE.md from
# the repository's unchanged sources. All output stays in ./build.
#
#   make            build build/drv and build/syms.txt
#   make clean
#
# The runtime is compiled with -finstrument-functions so that every emitted
# code cell is attributed to the C emitter that produced it. The S1 machine is
# an instrumented COPY (measure/s1_arch.c); the frozen s1.c is never changed.

ROOT  := ../..
BUILD := build
CC    ?= cc

DRV_CFLAGS  := -std=c17 -O1 -w -no-pie -I$(ROOT)
RT_CFLAGS   := -std=c17 -O0 -w -no-pie -finstrument-functions -I$(ROOT)
S1_CFLAGS   := -std=c17 -O1 -w -no-pie -I$(ROOT)

all: $(BUILD)/drv $(BUILD)/syms.txt

$(BUILD):
	@mkdir -p $@

$(BUILD)/s1_arch.o: measure/s1_arch.c $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) $(S1_CFLAGS) -c -o $@ $<

$(BUILD)/rt.o: $(ROOT)/r0_s1_runtime.c $(ROOT)/r0_s1.h $(ROOT)/s1.h | $(BUILD)
	$(CC) $(RT_CFLAGS) -c -o $@ $<

$(BUILD)/hooks.o: measure/hooks.c | $(BUILD)
	$(CC) $(S1_CFLAGS) -c -o $@ $<

$(BUILD)/drv: measure/drv.c $(BUILD)/rt.o $(BUILD)/s1_arch.o $(BUILD)/hooks.o \
              $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c | $(BUILD)
	$(CC) $(DRV_CFLAGS) -o $@ measure/drv.c $(BUILD)/rt.o $(BUILD)/s1_arch.o \
	  $(BUILD)/hooks.o $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c

# Regenerate after every relink: the dump records function addresses.
$(BUILD)/syms.txt: $(BUILD)/drv
	nm $(BUILD)/drv | grep -i " t " | awk '{print $$1, $$3}' > $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
