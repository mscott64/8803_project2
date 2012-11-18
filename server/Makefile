CC=gcc
CFLAGS=-I. -I.. -lpthread -Wall
DEPS = server.h constants.h
OBJ = shared.o server.o

all: server

shared.o: ../shared.c
	$(CC) -c ../shared.c

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

server: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean

clean: 
	rm -f *.o server