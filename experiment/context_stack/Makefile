CC = gcc
# -rdynamic: dladdr must resolve our own functions for the tool-frame skip
# -fno-optimize-sibling-calls: keep frames real/deterministic in the tests
CFLAGS = -O2 -g -rdynamic -fno-optimize-sibling-calls -Wall

all: test_correctness bench

test_correctness: test_correctness.c context_connect.c context_connect.h replay.h
	$(CC) $(CFLAGS) test_correctness.c context_connect.c -o $@ -ldl -lpthread

bench: bench.c context_connect.c context_connect.h
	$(CC) $(CFLAGS) bench.c context_connect.c -o $@ -ldl

run: all
	./test_correctness
	./bench

clean:
	rm -f test_correctness bench
