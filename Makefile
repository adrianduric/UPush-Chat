CFLAGS = -g -std=gnu11 -Wall -Wextra
VFLAGS = --leak-check=full --show-leak-kinds=all --track-origins=yes --malloc-fill=0x40 --free-fill=0x23
TESTARGS_SERVER = 2022 20
TESTARGS_CLIENT_1 = Kevin 127.0.0.1 2022 2 20
TESTARGS_CLIENT_2 = Angela 127.0.0.1 2022 2 20
TESTARGS_CLIENT_3 = Oscar 127.0.0.1 2022 2 20
BIN = upush_client upush_server

all: $(BIN)

upush_client: upush_client.o send_packet.o send_packet.h
	gcc $(CFLAGS) upush_client.o send_packet.o -o upush_client

upush_server: upush_server.o send_packet.o send_packet.h
	gcc $(CFLAGS) upush_server.c send_packet.o -o upush_server

upush_client.o: upush_client.c
	gcc $(CFLAGS) -c upush_client.c

upush_server.o: upush_server.c
	gcc $(CFLAGS) -c upush_server.c

send_packet.o: send_packet.c
	gcc $(CFLAGS) -c send_packet.c

check_client_1: upush_client
	valgrind $(VFLAGS) ./upush_client $(TESTARGS_CLIENT_1)

check_client_2: upush_client
	valgrind $(VFLAGS) ./upush_client $(TESTARGS_CLIENT_2)

check_client_3: upush_client
	valgrind $(VFLAGS) ./upush_client $(TESTARGS_CLIENT_3)

check_server: upush_server
	valgrind $(VFLAGS) ./upush_server $(TESTARGS_SERVER)

clean:
	rm -f $(BIN) *.o
