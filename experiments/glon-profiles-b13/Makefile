ROOT := ../..
B12 := ../glon-deflate-b12
CC ?= cc
CFLAGS := -std=c17 -O2 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -DGLON_LIVE -I$(ROOT) -I$(B12)
BUILD := build

all: $(BUILD)/glon-s1-7 $(BUILD)/glon-s1-12 $(BUILD)/glon-lib/prelude.glon \
     $(BUILD)/glon-lib/strings.glon $(BUILD)/traffic-s1-7 $(BUILD)/traffic-s1-12

$(BUILD):
	@mkdir -p $@

$(BUILD)/glon-s1-7: $(ROOT)/glon.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c \
                    $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(ROOT)/glon.c $(ROOT)/r0_s1_session.c \
	  $(ROOT)/r0_s1_show.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c

$(BUILD)/glon-s1-12: $(ROOT)/glon.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_show.c \
                     $(B12)/b12_runtime.c $(B12)/b12_machine.c $(B12)/b12_machine.h | $(BUILD)
	$(CC) $(CFLAGS) -DB12_NATIVE_RS=1 -o $@ $(ROOT)/glon.c $(ROOT)/r0_s1_session.c \
	  $(ROOT)/r0_s1_show.c $(B12)/b12_runtime.c $(B12)/b12_machine.c

$(BUILD)/glon-lib/prelude.glon: $(ROOT)/jupyter/prelude.glon | $(BUILD)
	@mkdir -p $(BUILD)/glon-lib; cp $< $@
$(BUILD)/glon-lib/strings.glon: $(ROOT)/demo/shop/strings.glon | $(BUILD)
	@mkdir -p $(BUILD)/glon-lib; cp $< $@


test: all
	cd $(ROOT) && ./check-frozen-s1.sh

clean:
	rm -rf $(BUILD)
