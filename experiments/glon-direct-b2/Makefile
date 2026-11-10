# experiments/glon-direct-b2/Makefile
#
# B2: native CALL / RETURN / >R / R> / R@ / BRANCH candidates on a separate
# experimental machine. B1 is the reference and is not modified.
#
#   make test       B1 tests + B2 tests + frozen check
#   make measure    B2 compiled workloads (all primitive configurations)
#   make compare    B1 reference measurement + B2 measurement + masm baseline
#   make size       compiler and machine sizes, B1 vs B2
#   make clean

ROOT  := ../..
B1    := ../glon-direct-b1
CC    ?= cc
BUILD := build

CFLAGS := -std=c17 -O2 -Wall -Wextra -I$(ROOT)
RUNTIME := $(ROOT)/r0_s1_runtime.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c

all: $(BUILD)/b2-test $(BUILD)/b2-measure $(BUILD)/baseline

$(BUILD):
	@mkdir -p $@

$(BUILD)/b2-test: b2_main.c b2_compiler.c b2_compiler.h b2_machine.c b2_machine.h \
                  $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ b2_main.c b2_compiler.c b2_machine.c

$(BUILD)/b2-measure: b2_main.c b2_compiler.c b2_compiler.h b2_machine.c b2_machine.h \
                     $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ b2_main.c b2_compiler.c b2_machine.c

$(BUILD)/baseline: baseline.c b2_machine.c b2_machine.h $(RUNTIME) \
                   $(ROOT)/r0_s1.h $(ROOT)/s1.h $(ROOT)/m1_layout.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ baseline.c b2_machine.c $(RUNTIME)

$(BUILD)/b2_compiler.o: b2_compiler.c b2_compiler.h $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ b2_compiler.c

$(BUILD)/b2_machine.o: b2_machine.c b2_machine.h $(ROOT)/s1.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ b2_machine.c

$(BUILD)/b1_compiler_ref.o: $(B1)/b1_compiler.c $(B1)/b1_compiler.h $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(B1) -c -o $@ $(B1)/b1_compiler.c

$(BUILD)/b1_machine_ref.o: $(B1)/s1_count.c $(B1)/s1_count.h $(ROOT)/s1.h | $(BUILD)
	$(CC) $(CFLAGS) -I$(B1) -c -o $@ $(B1)/s1_count.c

test: $(BUILD)/b2-test
	cd $(ROOT) && ./check-frozen-s1.sh
	$(MAKE) -C $(B1) test
	./$(BUILD)/b2-test

$(BUILD)/b2-test-asan: b2_main.c b2_compiler.c b2_compiler.h b2_machine.c b2_machine.h                        $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) -std=c17 -O1 -g -Wall -Wextra -fsanitize=address,undefined \
	  -I$(ROOT) -o $@ b2_main.c b2_compiler.c b2_machine.c

sanitize: $(BUILD)/b2-test-asan
	./$(BUILD)/b2-test-asan

measure: $(BUILD)/b2-measure $(BUILD)/baseline
	./$(BUILD)/b2-measure --measure
	./$(BUILD)/baseline $(ROOT)

compare: measure
	$(MAKE) -C $(B1) measure

size: $(BUILD)/b2_compiler.o $(BUILD)/b2_machine.o $(BUILD)/b1_compiler_ref.o $(BUILD)/b1_machine_ref.o
	@echo "source lines:"
	@wc -l b2_compiler.c b2_compiler.h b2_machine.c b2_machine.h
	@wc -l $(B1)/b1_compiler.c $(B1)/b1_compiler.h $(B1)/s1_count.c $(B1)/s1_count.h
	@echo "B2 compiler object:"
	@size $(BUILD)/b2_compiler.o
	@echo "B1 compiler object:"
	@size $(BUILD)/b1_compiler_ref.o
	@echo "B2 machine object (counters included):"
	@size $(BUILD)/b2_machine.o
	@echo "B1 machine counter copy:"
	@size $(BUILD)/b1_machine_ref.o
	@echo "frozen s1.c:"
	@wc -l $(ROOT)/s1.c $(ROOT)/s1.h

clean:
	rm -rf $(BUILD)

.PHONY: all test sanitize measure compare size clean
