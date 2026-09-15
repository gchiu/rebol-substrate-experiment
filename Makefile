CC      ?= cc
CFLAGS  ?= -std=c17 -Wall -Wextra -O0 -g

OBJS = s1.o tests.o adversarial.o claims.o r0.o r0_tests.o r0_s1_runtime.o r0_s1_tests.o main.o

all: s1

s1: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

s1.o: s1.c s1.h
tests.o: tests.c s1.h
adversarial.o: adversarial.c s1.h
claims.o: claims.c s1.h
r0.o: r0.c r0.h s1.h
r0_tests.o: r0_tests.c r0.h s1.h
r0_s1_runtime.o: r0_s1_runtime.c r0_s1.h s1.h
r0_s1_tests.o: r0_s1_tests.c r0_s1.h s1.h
main.o: main.c s1.h

test: s1
	./s1

clean:
	rm -f s1 $(OBJS)

.PHONY: all test clean
