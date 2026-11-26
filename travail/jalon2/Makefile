CFLAGS=-Wall
#LDFLAGS=-lpthread
all: client server

client: client.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

server: server.o commands.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

client.o: client.c client.h common.h msg_struct.h
server.o: server.c common.h commands.h msg_struct.h
commands.o: commands.c common.h commands.h msg_struct.h

clean:
	rm -f client server *.o
