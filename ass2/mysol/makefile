CXX=gcc
CFLAGS=-Wextra -Wall -Werror --std=c11

default: executables

debug: CFLAGS += -DDEBUG -g
debug: executables 

executables: bwtdecode

bwtdecode: bwtdecode.c
	$(CXX) $(CFLAGS) -o $@ $<

remake: clean default

clean:
	rm -f bwtdecode
