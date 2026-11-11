ROOT := ../..
CC ?= cc
CFLAGS := -std=c17 -O2 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -DGLON_LIVE -I$(ROOT) -I.
BUILD := build

all: $(BUILD)/dzhost-seven-o2 $(BUILD)/dzhost-nine $(BUILD)/dzhost-twelve \
     $(BUILD)/dzhost-nine-count $(BUILD)/dzhost-twelve-count \
     $(BUILD)/glon-nine $(BUILD)/glon-twelve $(BUILD)/glon-lib/prelude.glon $(BUILD)/glon-lib/strings.glon

$(BUILD):
	@mkdir -p $@

$(BUILD)/dzhost-seven-o2: ../glon-deflate/dzhost.c $(ROOT)/r0_s1_g1a_live.c $(ROOT)/r0_s1_g1a.c \
                         $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ ../glon-deflate/dzhost.c $(ROOT)/r0_s1_g1a_live.c \
	  $(ROOT)/r0_s1_g1a.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c

$(BUILD)/dzhost-nine: b12_host.c b12_runtime.c b12_machine.c b12_machine.h | $(BUILD)
	$(CC) $(CFLAGS) -DB12_NATIVE_RS=0 -o $@ b12_host.c $(ROOT)/r0_s1_g1a_live.c \
	  $(ROOT)/r0_s1_g1a.c b12_runtime.c b12_machine.c

$(BUILD)/dzhost-twelve: b12_host.c b12_runtime.c b12_machine.c b12_machine.h | $(BUILD)
	$(CC) $(CFLAGS) -DB12_NATIVE_RS=1 -o $@ b12_host.c $(ROOT)/r0_s1_g1a_live.c \
	  $(ROOT)/r0_s1_g1a.c b12_runtime.c b12_machine.c

$(BUILD)/dzhost-nine-count: b12_host.c b12_runtime.c b12_machine.c b12_machine.h | $(BUILD)
	$(CC) $(CFLAGS) -DB12_NATIVE_RS=0 -DB3_COUNT -DB11_COUNT -o $@ b12_host.c \
	  $(ROOT)/r0_s1_g1a_live.c $(ROOT)/r0_s1_g1a.c b12_runtime.c b12_machine.c

$(BUILD)/dzhost-twelve-count: b12_host.c b12_runtime.c b12_machine.c b12_machine.h | $(BUILD)
	$(CC) $(CFLAGS) -DB12_NATIVE_RS=1 -DB3_COUNT -DB11_COUNT -o $@ b12_host.c \
	  $(ROOT)/r0_s1_g1a_live.c $(ROOT)/r0_s1_g1a.c b12_runtime.c b12_machine.c

$(BUILD)/glon-nine: $(ROOT)/glon.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c b12_runtime.c b12_machine.c b12_machine.h | $(BUILD)
	$(CC) -std=c17 -O1 -Wall -Wextra -DB12_NATIVE_RS=0 -I$(ROOT) -I. -o $@ $(ROOT)/glon.c \
	  $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c b12_runtime.c b12_machine.c
$(BUILD)/glon-twelve: $(ROOT)/glon.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c b12_runtime.c b12_machine.c b12_machine.h | $(BUILD)
	$(CC) -std=c17 -O1 -Wall -Wextra -DB12_NATIVE_RS=1 -I$(ROOT) -I. -o $@ $(ROOT)/glon.c \
	  $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c b12_runtime.c b12_machine.c

$(BUILD)/glon-lib/prelude.glon: $(ROOT)/jupyter/prelude.glon | $(BUILD)
	@mkdir -p $(BUILD)/glon-lib; cp $< $@
$(BUILD)/glon-lib/strings.glon: $(ROOT)/demo/shop/strings.glon | $(BUILD)
	@mkdir -p $(BUILD)/glon-lib; cp $< $@

$(BUILD)/b12-guard-nine: guard.c b12_machine.c b12_machine.h | $(BUILD)
	$(CC) $(CFLAGS) -DB12_NATIVE_RS=0 -o $@ guard.c b12_machine.c

unit: all $(BUILD)/b12-guard-nine
	cd $(ROOT)/experiments/glon-deflate && \
	  ../glon-deflate-b12/$(BUILD)/glon-nine ../../demo/shop/strings-ext.glon f2buf.glon inflate.glon tests/unit-tests.glon && \
	  ../glon-deflate-b12/$(BUILD)/glon-twelve ../../demo/shop/strings-ext.glon f2buf.glon inflate.glon tests/unit-tests.glon
	./$(BUILD)/b12-guard-nine

bench: all
	python3 bench_b12.py

test: all unit
	cd $(ROOT)/experiments/glon-deflate && \
	  python3 -I tests/run_tests.py --host ../glon-deflate-b12/$(BUILD)/dzhost-nine && \
	  python3 -I tests/run_tests.py --host ../glon-deflate-b12/$(BUILD)/dzhost-twelve
	cd $(ROOT) && ./check-frozen-s1.sh

clean:
	rm -rf $(BUILD)
