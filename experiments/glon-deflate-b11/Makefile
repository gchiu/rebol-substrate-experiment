ROOT := ../..
CC ?= cc
CFLAGS := -std=c17 -O2 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -DGLON_LIVE -I$(ROOT) -I.
BUILD := build

all: $(BUILD)/dzhost-seven-o2 $(BUILD)/dzhost-b11 $(BUILD)/dzhost-b11-count $(BUILD)/glon-b11 $(BUILD)/glon-lib/prelude.glon $(BUILD)/glon-lib/strings.glon

$(BUILD):
	@mkdir -p $@

$(BUILD)/dzhost-seven-o2: ../glon-deflate/dzhost.c $(ROOT)/r0_s1_g1a_live.c $(ROOT)/r0_s1_g1a.c \
                         $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ ../glon-deflate/dzhost.c $(ROOT)/r0_s1_g1a_live.c \
	  $(ROOT)/r0_s1_g1a.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c

$(BUILD)/dzhost-b11: b11_host.c b11_runtime.c b11_machine.c b11_machine.h \
                     $(ROOT)/r0_s1_g1a_live.c $(ROOT)/r0_s1_g1a.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ b11_host.c $(ROOT)/r0_s1_g1a_live.c \
	  $(ROOT)/r0_s1_g1a.c b11_runtime.c b11_machine.c

$(BUILD)/dzhost-b11-count: b11_host.c b11_runtime.c b11_machine.c b11_machine.h \
                           $(ROOT)/r0_s1_g1a_live.c $(ROOT)/r0_s1_g1a.c | $(BUILD)
	$(CC) $(CFLAGS) -DB3_COUNT -DB11_COUNT -o $@ b11_host.c $(ROOT)/r0_s1_g1a_live.c \
	  $(ROOT)/r0_s1_g1a.c b11_runtime.c b11_machine.c

$(BUILD)/glon-b11: $(ROOT)/glon.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c \
                   b11_runtime.c b11_machine.c b11_machine.h | $(BUILD)
	$(CC) -std=c17 -O1 -Wall -Wextra -I$(ROOT) -I. -o $@ $(ROOT)/glon.c \
	  $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c b11_runtime.c b11_machine.c

$(BUILD)/glon-lib/prelude.glon: $(ROOT)/jupyter/prelude.glon | $(BUILD)
	@mkdir -p $(BUILD)/glon-lib
	cp $< $@
$(BUILD)/glon-lib/strings.glon: $(ROOT)/demo/shop/strings.glon | $(BUILD)
	@mkdir -p $(BUILD)/glon-lib
	cp $< $@

unit: all
	cd $(ROOT)/experiments/glon-deflate && ../glon-deflate-b11/$(BUILD)/glon-b11 \
	  ../../demo/shop/strings-ext.glon f2buf.glon inflate.glon tests/unit-tests.glon

bench: all
	python3 bench_b11.py

test: all
	cd $(ROOT)/experiments/glon-deflate && python3 -I tests/run_tests.py \
	  --host ../glon-deflate-b11/$(BUILD)/dzhost-b11
	cd $(ROOT) && ./check-frozen-s1.sh

clean:
	rm -rf $(BUILD)
