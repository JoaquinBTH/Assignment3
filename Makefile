CC = gcc
CC_FLAGS = -w -g



all: test client server


main.o: main.c
	$(CC) -Wall -I. -c main.c

client.o: client.c
	$(CC) -Wall -I. -c client.c

server.o: server.c
	$(CC) -Wall -I. -c server.c

test: main.o
	$(CC) -I./ -Wall -lncurses  -o test main.o 


client: client.o
	$(CC) -Wall -o cchat client.o

server: server.o
	$(CC) -Wall -o cserverd server.o


clean:
	rm *.o *.a test server client
