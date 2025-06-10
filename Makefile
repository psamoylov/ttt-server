CC=gcc
CFLAGS=-Wall -g -Wextra -pedantic -pthread -std=c99 -fsanitize=address,undefined

all: ttts ttt

ttts: ttts.c
	$(CC) $(CFLAGS) ttts.c -o ttts

ttt: cli.c
	$(CC) $(CFLAGS) cli.c -o ttt

clean:
	rm -f ttts ttt
