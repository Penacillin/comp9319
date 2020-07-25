CXX=gcc
CFLAGS=-Wextra -Wall -Werror --std=c11 -march=core-avx2 -march=native
LDFLAGS=-lrt

default: CFLAGS += -O3
default: executables

debug: CFLAGS += -DDEBUG -DPERF -g
debug: executables 

perf: CFLAGS += -O3 -g -pg -DPERF
perf: executables

executables: bwtdecode bwtsearch

bwtdecode: bwtdecode.c vector_utils.h
	$(CXX) $(CFLAGS) -o $@ $< $(LDFLAGS)

bwtsearch: bwtsearch.c vector_utils.h
	$(CXX) $(CFLAGS) -o $@ $< $(LDFLAGS)

remake: clean default

clean:
	rm -f bwtdecode
	rm -f bwtsearch
