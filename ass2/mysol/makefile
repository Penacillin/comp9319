CXX=gcc
CFLAGS=-Wextra -Wall -Werror --std=c11
LDFLAGS=-lrt

default: CFLAGS += -O3
default: executables

debug: CFLAGS += -DDEBUG -g
debug: executables 

perf: CFLAGS += -O3 -g
perf: executables

executables: bwtdecode

bwtdecode: bwtdecode.c
	$(CXX) $(CFLAGS) -o $@ $< $(LDFLAGS)

remake: clean default

clean:
	rm -f bwtdecode
