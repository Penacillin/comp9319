CC=g++
CFLAGS=-Wextra -Wall -Werror

default: executables

debug: CFLAGS += -DDEBUG -g
debug: executables 

executables: aencode adecode

aencode: aencode.c
	$(CC) $(CFLAGS) -o $@ $^ -lmpfr -lgmp

adecode: adecode.c
	$(CC) $(CFLAGS) -o $@ $^ -lmpfr -lgmp

remake: clean all

clean:
	rm -f aencode adecode