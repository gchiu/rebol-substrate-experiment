ROOT := ../..
CC ?= cc
CFLAGS := -std=c17 -O2 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -DGLON_LIVE -I$(ROOT)
BUILD := build

all: $(BUILD)/b10-host-ops $(BUILD)/b10-host-pf

$(BUILD):
	@mkdir -p $@

$(BUILD)/b10-host-ops: b10_host.c $(ROOT)/r0_s1_g1a_live.c $(ROOT)/r0_s1_g1a.c \
                       $(ROOT)/r0_s1_runtime.c $(ROOT)/s1_prof.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ b10_host.c $(ROOT)/r0_s1_g1a_live.c $(ROOT)/r0_s1_g1a.c \
	  $(ROOT)/r0_s1_runtime.c $(ROOT)/s1_prof.c

$(BUILD)/b10-host-pf: b10_host.c $(ROOT)/r0_s1_g1a_live.c $(ROOT)/r0_s1_g1a.c \
                      $(ROOT)/r0_s1_runtime.c $(ROOT)/s1_prof.c | $(BUILD)
	$(CC) $(CFLAGS) -DR0_S1_PROFILE -o $@ b10_host.c $(ROOT)/r0_s1_g1a_live.c \
	  $(ROOT)/r0_s1_g1a.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1_prof.c

profile: all
	python3 profile_workloads.py

test: profile
	cd $(ROOT) && ./check-frozen-s1.sh
	./$(BUILD)/b10-host-ops -L $(ROOT) -c 537 -r 65536 -s -o /tmp/b10-test.out fixtures/fixed.deflate >/dev/null 2>/tmp/b10-test.log
	@grep -q 'B10_OPS' /tmp/b10-test.log
	@echo 'b10: PASS'

clean:
	rm -rf $(BUILD)
