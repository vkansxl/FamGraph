CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2

famgraph: server.c family.c family.h
	$(CC) $(CFLAGS) server.c family.c -o famgraph

run: famgraph
	./famgraph

clean:
	rm -f famgraph

.PHONY: run clean
