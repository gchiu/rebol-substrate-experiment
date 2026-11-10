# experiments/glon-direct-b3/Makefile
#
# B3: wall-clock comparison of B1, B2 primitive configurations, and two
# calling conventions. B1 and B2 are unchanged references.
#
#   make test       frozen check + B1 tests + B2 tests + B3 tests
#   make measure    B1 and B3 instruction/code/stack measurements
#   make bench      wall-clock medians for B1 and B3 configurations
#   make reproduce  run the unchanged B1 and B2 measurements
#   make size       compiler and machine sizes
#   make sanitize   B3 tests under ASan/UBSan
#   make clean

ROOT  := ../..
B1    := ../glon-direct-b1
B2    := ../glon-direct-b2
CC    ?= cc
BUILD := build

CFLAGS := -std=c17 -O2 -Wall -Wextra -I$(ROOT) -I$(B1) -I$(B2)

$(BUILD):
	@mkdir -p $@

$(BUILD)/b3: b3_main.c b3_compiler.c b3_compiler.h b3_machine.c b3_machine.h bench_util.h \
             $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ b3_main.c b3_compiler.c b3_machine.c

$(BUILD)/b3-count: b3_main.c b3_compiler.c b3_compiler.h b3_machine.c b3_machine.h bench_util.h \
                   $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) -DB3_COUNT -o $@ b3_main.c b3_compiler.c b3_machine.c

$(BUILD)/b1-ref: b1_ref_main.c $(B1)/b1_compiler.c $(B1)/b1_compiler.h \
                 $(ROOT)/s1.c $(ROOT)/s1.h $(ROOT)/r0_s1.h bench_util.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ b1_ref_main.c $(B1)/b1_compiler.c $(ROOT)/s1.c

$(BUILD)/b1-ref-count: b1_ref_main.c $(B1)/b1_compiler.c $(B1)/b1_compiler.h \
                       $(B1)/s1_count.c $(B1)/s1_count.h $(ROOT)/s1.h $(ROOT)/r0_s1.h bench_util.h | $(BUILD)
	$(CC) $(CFLAGS) -DWITH_COUNTS -o $@ b1_ref_main.c $(B1)/b1_compiler.c $(B1)/s1_count.c

$(BUILD)/b3-test-asan: b3_main.c b3_compiler.c b3_compiler.h b3_machine.c b3_machine.h \
                       bench_util.h $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) -std=c17 -O1 -g -Wall -Wextra -fsanitize=address,undefined \
	  -I$(ROOT) -I$(B1) -I$(B2) -o $@ b3_main.c b3_compiler.c b3_machine.c

test: $(BUILD)/b3
	cd $(ROOT) && ./check-frozen-s1.sh
	$(MAKE) -C $(B1) test
	$(MAKE) -C $(B2) test
	./$(BUILD)/b3

sanitize: $(BUILD)/b3-test-asan
	./$(BUILD)/b3-test-asan

measure: $(BUILD)/b1-ref-count $(BUILD)/b3-count
	./$(BUILD)/b1-ref-count --measure
	./$(BUILD)/b3-count --measure

bench: $(BUILD)/b1-ref $(BUILD)/b3
	./$(BUILD)/b1-ref --bench
	./$(BUILD)/b3 --bench

BENCH_ROUNDS ?= 3
bench-rounds: $(BUILD)/b1-ref $(BUILD)/b3
	@for i in $$(seq 1 $(BENCH_ROUNDS)); do \
	  echo "===== ROUND $$i B1 ====="; ./$(BUILD)/b1-ref --bench; \
	  echo "===== ROUND $$i B3 ====="; ./$(BUILD)/b3 --bench; \
	done

reproduce:
	$(MAKE) -C $(B1) measure
	$(MAKE) -C $(B2) measure

size:
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $(BUILD)/b1_compiler.o $(B1)/b1_compiler.c
	$(CC) $(CFLAGS) -c -o $(BUILD)/b2_compiler.o $(B2)/b2_compiler.c
	$(CC) $(CFLAGS) -c -o $(BUILD)/b3_compiler.o b3_compiler.c
	$(CC) $(CFLAGS) -c -o $(BUILD)/b1_machine.o $(ROOT)/s1.c
	$(CC) $(CFLAGS) -DB3_COUNT -c -o $(BUILD)/b2_machine.o $(B2)/b2_machine.c
	$(CC) $(CFLAGS) -c -o $(BUILD)/b3_machine_fast.o b3_machine.c
	$(CC) $(CFLAGS) -DB3_COUNT -c -o $(BUILD)/b3_machine_count.o b3_machine.c
	@echo "source lines:"
	@wc -l b3_compiler.c b3_compiler.h b3_machine.c b3_machine.h b3_main.c b1_ref_main.c bench_util.h
	@echo "B1 compiler:"
	@size $(BUILD)/b1_compiler.o
	@echo "B2 compiler:"
	@size $(BUILD)/b2_compiler.o
	@echo "B3 compiler:"
	@size $(BUILD)/b3_compiler.o
	@echo "B1 machine (frozen s1.c, no counters):"
	@size $(BUILD)/b1_machine.o
	@echo "B2 machine (counters, as committed):"
	@size $(BUILD)/b2_machine.o
	@echo "B3 machine fast:"
	@size $(BUILD)/b3_machine_fast.o
	@echo "B3 machine count:"
	@size $(BUILD)/b3_machine_count.o

clean:
	rm -rf $(BUILD)

.PHONY: test sanitize measure bench bench-rounds reproduce size clean
