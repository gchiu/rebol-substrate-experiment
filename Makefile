CC      ?= cc
CFLAGS  ?= -std=c17 -Wall -Wextra -O0 -g

OBJS = s1.o tests.o main.o

all: s1

s1: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

s1.o: s1.c s1.h
tests.o: tests.c s1.h
main.o: main.c s1.h

test: s1
	./s1

clean:
	rm -f s1 $(OBJS)

.PHONY: all test clean
