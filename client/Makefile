CC=gcc
CFLAGS=-I. -I.. -lpthread -Wall
DEPS = client.h constants.h
OBJ = client.o

all: client

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

client: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean

clean: 
	rm -f *.o client