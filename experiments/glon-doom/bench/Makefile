ROOT := ../../..
CC ?= cc
CFLAGS ?= -std=c17 -O2 -Wall -Wextra -I$(ROOT)
TARGET := execution-bench
all: $(TARGET)
$(TARGET): execution-bench.c $(ROOT)/r0_s1.h $(ROOT)/m1_layout.h $(ROOT)/s1.h $(ROOT)/s1_prof.h
	$(CC) $(CFLAGS) -o $@ execution-bench.c $(ROOT)/r0_s1_session.c $(ROOT)/r0_s1_runtime.c $(ROOT)/s1.c -lm
clean:
	rm -f $(TARGET)
.PHONY: all clean
