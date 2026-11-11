ROOT := ../..
B3 := ../glon-direct-b3
C1 := ../glon-compiler-c1
CC ?= cc
CFLAGS := -std=c17 -O2 -Wall -Wextra -I. -I$(C1) -I$(B3) -I$(ROOT)
BUILD := build

all: $(BUILD)/dzhost-b9 $(BUILD)/b9-kernel-count $(BUILD)/b9-kernel $(BUILD)/b9-coverage

$(BUILD):
	@mkdir -p $@

$(BUILD)/dzhost-b9: baseline/dzhost_b9.c $(ROOT)/r0_s1_runtime.c $(ROOT)/r0_s1_g1a_live.c \
                    $(ROOT)/r0_s1_g1a.c $(ROOT)/s1_prof.c $(ROOT)/r0_s1.h $(ROOT)/s1.h | $(BUILD)
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L -DGLON_LIVE -o $@ \
	  baseline/dzhost_b9.c $(ROOT)/r0_s1_runtime.c $(ROOT)/r0_s1_g1a_live.c \
	  $(ROOT)/r0_s1_g1a.c $(ROOT)/s1_prof.c

$(BUILD)/b9-kernel-count: b9_kernel.c b9_machine.c b9_machine.h $(C1)/c1_compiler.c $(C1)/c1_compiler.h | $(BUILD)
	$(CC) $(CFLAGS) -DB3_COUNT -o $@ b9_kernel.c $(C1)/c1_compiler.c b9_machine.c

$(BUILD)/b9-kernel: b9_kernel.c b9_machine.c b9_machine.h $(C1)/c1_compiler.c $(C1)/c1_compiler.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ b9_kernel.c $(C1)/c1_compiler.c b9_machine.c

$(BUILD)/b9-coverage: coverage.c $(C1)/c1_compiler.c $(C1)/c1_compiler.h b9_machine.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ coverage.c $(C1)/c1_compiler.c b9_machine.c

baseline: $(BUILD)/dzhost-b9
	python3 bench_baseline.py

kernel: $(BUILD)/b9-kernel-count $(BUILD)/b9-kernel
	./$(BUILD)/b9-kernel-count --measure none
	./$(BUILD)/b9-kernel-count --measure candidate
	./$(BUILD)/b9-kernel --bench none
	./$(BUILD)/b9-kernel --bench candidate

coverage: $(BUILD)/b9-coverage
	./$(BUILD)/b9-coverage $(ROOT)/experiments/glon-deflate/inflate.glon || true
	./$(BUILD)/b9-coverage $(ROOT)/experiments/glon-deflate/app.glon || true

test: $(BUILD)/b9-kernel-count $(BUILD)/b9-coverage
	./$(BUILD)/b9-kernel-count --measure none >/tmp/b9-seven.json
	./$(BUILD)/b9-kernel-count --measure candidate >/tmp/b9-twelve.json
	./$(BUILD)/b9-coverage $(ROOT)/experiments/glon-deflate/inflate.glon || true
	@echo "B9 kernel test outputs in /tmp/b9-seven.json and /tmp/b9-twelve.json"

clean:
	rm -rf $(BUILD) baseline/results.json baseline/run.log
