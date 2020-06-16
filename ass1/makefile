default: aencode adecode


aencode: aencode.c
	g++ -g -gdwarf-2 -o aencode aencode.c -lmpfr -lgmp

adecode: adecode.c
	g++ -g -gdwarf-2 -o adecode adecode.c -lmpfr -lgmp

clean:
	rm -f aencode adecode