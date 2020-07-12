CXX=gcc
CFLAGS=-Wextra -Wall -Werror --std=c11
LDFLAGS=-lrt

default: CFLAGS += -O3 -s
default: executables

debug: CFLAGS += -DDEBUG -g
debug: executables 

executables: bwtdecode

bwtdecode: bwtdecode.c
	$(CXX) $(CFLAGS) -o $@ $< $(LDFLAGS)

remake: clean default

clean:
	rm -f bwtdecode
