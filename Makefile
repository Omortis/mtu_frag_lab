CC = gcc
CFLAGS = -Wall -Wextra -O2

TARGETS = encapsulator decapsulator http_server

all: $(TARGETS)

encapsulator: encapsulator.c common.h
	$(CC) $(CFLAGS) -o $@ encapsulator.c

decapsulator: decapsulator.c common.h
	$(CC) $(CFLAGS) -o $@ decapsulator.c

http_server: http_server.c
	$(CC) $(CFLAGS) -o $@ http_server.c

clean:
	rm -f $(TARGETS)

.PHONY: all clean
