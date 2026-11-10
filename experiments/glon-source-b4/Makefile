# experiments/glon-source-b4/Makefile
#
# B4: compare textual Glon, semantically tagged tokens, and direct machine
# words on the common B3 target. B1-B3 are unchanged references.
#
#   make fixtures   regenerate the task fixtures from tools/gen_fixtures.py
#   make test       frozen check + B1/B2/B3 tests + B4 tests
#   make measure    source tokens/bytes, code, instructions, stacks
#   make bench      source-processing and execution times
#   make reproduce  run the unchanged B1/B2/B3 measurements
#   make size       compiler/decoder/assembler sizes
#   make sanitize   B4 tests under ASan/UBSan

ROOT  := ../..
B1    := ../glon-direct-b1
B2    := ../glon-direct-b2
B3    := ../glon-direct-b3
CC    ?= cc
BUILD := build

CFLAGS := -std=c17 -O2 -Wall -Wextra -I$(ROOT) -I$(B3)

$(BUILD):
	@mkdir -p $@

$(BUILD)/b4: b4_main.c b4_tagged.c b4_direct.c b4_common.h b4_tagged.h b4_direct.h \
             bench_util.h $(B3)/b3_compiler.c $(B3)/b3_compiler.h \
             $(B3)/b3_machine.c $(B3)/b3_machine.h $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ b4_main.c b4_tagged.c b4_direct.c \
	  $(B3)/b3_compiler.c $(B3)/b3_machine.c

$(BUILD)/b4-count: b4_main.c b4_tagged.c b4_direct.c b4_common.h b4_tagged.h b4_direct.h \
                   bench_util.h $(B3)/b3_compiler.c $(B3)/b3_compiler.h \
                   $(B3)/b3_machine.c $(B3)/b3_machine.h $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) -DB3_COUNT -DB4_COUNT -o $@ b4_main.c b4_tagged.c b4_direct.c \
	  $(B3)/b3_compiler.c $(B3)/b3_machine.c

$(BUILD)/b4-test-asan: b4_main.c b4_tagged.c b4_direct.c b4_common.h b4_tagged.h b4_direct.h \
                       bench_util.h $(B3)/b3_compiler.c $(B3)/b3_compiler.h \
                       $(B3)/b3_machine.c $(B3)/b3_machine.h $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) -std=c17 -O1 -g -Wall -Wextra -fsanitize=address,undefined \
	  -I$(ROOT) -I$(B3) -o $@ b4_main.c b4_tagged.c b4_direct.c \
	  $(B3)/b3_compiler.c $(B3)/b3_machine.c

fixtures:
	python3 tools/gen_fixtures.py

test: $(BUILD)/b4
	cd $(ROOT) && ./check-frozen-s1.sh
	$(MAKE) -C $(B1) test
	$(MAKE) -C $(B2) test
	$(MAKE) -C $(B3) test
	./$(BUILD)/b4 --test

measure: $(BUILD)/b4-count
	./$(BUILD)/b4-count --measure

bench: $(BUILD)/b4
	./$(BUILD)/b4 --bench

sanitize: $(BUILD)/b4-test-asan
	./$(BUILD)/b4-test-asan --test

reproduce:
	$(MAKE) -C $(B1) measure
	$(MAKE) -C $(B2) measure
	$(MAKE) -C $(B3) measure

size:
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $(BUILD)/b3_compiler_ref.o $(B3)/b3_compiler.c
	$(CC) $(CFLAGS) -c -o $(BUILD)/b4_tagged.o b4_tagged.c
	$(CC) $(CFLAGS) -c -o $(BUILD)/b4_direct.o b4_direct.c
	$(CC) $(CFLAGS) -c -o $(BUILD)/b3_machine_fast.o $(B3)/b3_machine.c
	@echo "source lines:"
	@wc -l b4_tagged.c b4_tagged.h b4_direct.c b4_direct.h b4_main.c b4_common.h
	@echo "A: B3 compiler (used by A):"
	@size $(BUILD)/b3_compiler_ref.o
	@echo "B: tagged decoder/emitter:"
	@size $(BUILD)/b4_tagged.o
	@echo "C: direct assembler:"
	@size $(BUILD)/b4_direct.o
	@echo "common B3 machine (fast):"
	@size $(BUILD)/b3_machine_fast.o

clean:
	rm -rf $(BUILD)

.PHONY: fixtures test measure bench sanitize reproduce size clean
