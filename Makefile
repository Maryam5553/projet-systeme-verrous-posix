CC = gcc
CFLAGS = -Wall -g
LDFLAGS= -lrt -pthread

all: test test2 remove

.PHONY: all

test: test.o rl_lock_library.o 
	$(CC) -o $@ $^ $(LDFLAGS)

test2: test2.o rl_lock_library.o
	$(CC) -o $@ $^ $(LDFLAGS)

test.o: test.c
	$(CC) -c $^ $(CFLAGS)

test2.o : test2.c
	$(CC) -c $^ $(CFLAGS)

rl_lock_library.o: rl_lock_library.c rl_lock_library.h panic.h
	$(CC) -c $^ $(CFLAGS)

remove:
	rm -rf *.o *.gch

clean:
	rm -rf *.o *.gch
	rm -rf test test2
