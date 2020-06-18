CC=g++
CFLAGS=-Wextra -Wall -Werror --std=c++11

default: executables

debug: CFLAGS += -DDEBUG -g
debug: executables 

executables: aencode adecode

aencode: aencode.cpp utilities.hpp
	$(CC) $(CFLAGS) -o $@ $< -lmpfr -lgmp

adecode: adecode.cpp utilities.hpp
	$(CC) $(CFLAGS) -o $@ $< -lmpfr -lgmp

remake: clean default

clean:
	rm -f aencode adecode