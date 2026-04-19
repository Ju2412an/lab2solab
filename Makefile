CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L

all: wish

wish: wish.c
	$(CC) $(CFLAGS) -o wish wish.c

clean:
	rm -f wish

.PHONY: all clean
