# experiments/glon-authoring-b5/Makefile
#
# B5: tasks, prompts, regression harness and trial runner for LLM authoring
# using the B4 representations. No LLM was available; no trials are simulated.
#
#   make fixtures   regenerate B5 task artifacts
#   make test       frozen check + B1-B4 regression + B5 task tests
#   make measure    source sizes/tokens, code, instructions, stacks
#   make bench      source-processing and execution times for reference artifacts
#   make size       B5 harness and reused front-end/machine sizes
#   make sanitize   B5 tests under ASan/UBSan

ROOT  := ../..
B1    := ../glon-direct-b1
B2    := ../glon-direct-b2
B3    := ../glon-direct-b3
B4    := ../glon-source-b4
CC    ?= cc
BUILD := build

CFLAGS := -std=c17 -O2 -Wall -Wextra -I$(ROOT) -I$(B3) -I$(B4)

$(BUILD):
	@mkdir -p $@

$(BUILD)/b5: b5_main.c $(B4)/b4_tagged.c $(B4)/b4_tagged.h $(B4)/b4_direct.c \
             $(B4)/b4_direct.h $(B4)/b4_common.h bench_util.h \
             $(B3)/b3_compiler.c $(B3)/b3_compiler.h \
             $(B3)/b3_machine.c $(B3)/b3_machine.h $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ b5_main.c $(B4)/b4_tagged.c $(B4)/b4_direct.c \
	  $(B3)/b3_compiler.c $(B3)/b3_machine.c

$(BUILD)/b5-count: b5_main.c $(B4)/b4_tagged.c $(B4)/b4_tagged.h $(B4)/b4_direct.c \
                   $(B4)/b4_direct.h $(B4)/b4_common.h bench_util.h \
                   $(B3)/b3_compiler.c $(B3)/b3_compiler.h \
                   $(B3)/b3_machine.c $(B3)/b3_machine.h $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) $(CFLAGS) -DB3_COUNT -DB5_COUNT -o $@ b5_main.c $(B4)/b4_tagged.c \
	  $(B4)/b4_direct.c $(B3)/b3_compiler.c $(B3)/b3_machine.c

$(BUILD)/b5-test-asan: b5_main.c $(B4)/b4_tagged.c $(B4)/b4_tagged.h $(B4)/b4_direct.c \
                       $(B4)/b4_direct.h $(B4)/b4_common.h bench_util.h \
                       $(B3)/b3_compiler.c $(B3)/b3_compiler.h \
                       $(B3)/b3_machine.c $(B3)/b3_machine.h $(ROOT)/s1.h $(ROOT)/r0_s1.h | $(BUILD)
	$(CC) -std=c17 -O1 -g -Wall -Wextra -fsanitize=address,undefined \
	  -I$(ROOT) -I$(B3) -I$(B4) -o $@ b5_main.c $(B4)/b4_tagged.c \
	  $(B4)/b4_direct.c $(B3)/b3_compiler.c $(B3)/b3_machine.c

fixtures:
	python3 tools/gen_tasks.py

test: $(BUILD)/b5
	cd $(ROOT) && ./check-frozen-s1.sh
	$(MAKE) -C $(B4) test
	./$(BUILD)/b5 --test

measure: $(BUILD)/b5-count
	./$(BUILD)/b5-count --measure

bench: $(BUILD)/b5
	./$(BUILD)/b5 --bench

sanitize: $(BUILD)/b5-test-asan
	./$(BUILD)/b5-test-asan --test

size:
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $(BUILD)/b5_main.o b5_main.c
	$(CC) $(CFLAGS) -c -o $(BUILD)/b4_tagged.o $(B4)/b4_tagged.c
	$(CC) $(CFLAGS) -c -o $(BUILD)/b4_direct.o $(B4)/b4_direct.c
	$(CC) $(CFLAGS) -c -o $(BUILD)/b3_compiler.o $(B3)/b3_compiler.c
	$(CC) $(CFLAGS) -c -o $(BUILD)/b3_machine.o $(B3)/b3_machine.c
	@echo "source lines:"
	@wc -l b5_main.c tools/gen_tasks.py
	@echo "B5 harness:"
	@size $(BUILD)/b5_main.o
	@echo "A front-end (B3 compiler):"
	@size $(BUILD)/b3_compiler.o
	@echo "B front-end (B4 tagged):"
	@size $(BUILD)/b4_tagged.o
	@echo "C front-end (B4 direct):"
	@size $(BUILD)/b4_direct.o
	@echo "common machine:"
	@size $(BUILD)/b3_machine.o

clean:
	rm -rf $(BUILD)

.PHONY: fixtures test measure bench sanitize size clean
