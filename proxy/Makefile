CC=gcc
CFLAGS=-I. -I.. -lpthread -Wall
DEPS = proxy.h constants.h
OBJ = shared.o proxy.o

all: proxy

shared.o: ../shared.c
	$(CC) -c ../shared.c

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

proxy: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean

clean: 
	rm -f *.o proxy