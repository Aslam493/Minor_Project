# Normal-World clients for the Secure CAN Monitor (Component 2.2). Does not touch spi_poc_test.
# Cross build for the Pi:  make CROSS_COMPILE=<toolchain>/aarch64-linux-gnu- TEEC_EXPORT=<optee_client export dir>
# (compile check only here; running on the Pi needs separate approval).
CC := $(CROSS_COMPILE)gcc
OS := ../../../optee_full/optee_os
TEEC_INC ?= ../../../optee_full/optee_client/libteec/include
CFLAGS += -std=gnu11 -Wall -Wextra -Werror -O2 -I$(TEEC_INC) -I$(OS)/lib/libutee/include
LDADD  += -L$(TEEC_EXPORT)/lib -lteec

all: mon_client atomic_test c5_board_tests c6_negative_raw
mon_client: mon_cli.o mon_client.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDADD)
c5_board_tests: c5_board_tests.c mon_client.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDADD) -lpthread
c6_negative_raw: c6_negative_raw.c mon_client.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDADD)
atomic_test: atomic_test.c
	$(CC) $(CFLAGS) -o $@ $< -lpthread
%.o: %.c mon_client.h
	$(CC) $(CFLAGS) -c $< -o $@
clean:
	rm -f *.o mon_client atomic_test c5_board_tests c6_negative_raw

# canbridge (2026-11-18): the packet is made by can_sender, travels through can0, and can_forwarder submits it (command 17).
canbridge.o: canbridge.c canbridge.h mon_client.h
	$(CC) $(CFLAGS) -c canbridge.c -o $@
can_sender: can_sender.c canbridge.o mon_client.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDADD)
can_forwarder: can_forwarder.c canbridge.o mon_client.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDADD)
# host test (native compiler, link errors for TEEC symbols ignored because the test never calls them)
canbridge_test: canbridge_test.c canbridge.c mon_client.c canbridge.h mon_client.h
	gcc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I$(TEEC_INC) -I$(OS)/lib/libutee/include -Wl,--unresolved-symbols=ignore-all -o $@ canbridge_test.c canbridge.c mon_client.c
can_replay: can_replay.c canbridge.o mon_client.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDADD)

# Mode P request/reply protocol test (2026-11-18): deliberate protocol mistakes, board only
pq_protocol_test: pq_protocol_test.c mon_client.o mon_client.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ pq_protocol_test.c mon_client.o $(LDADD)
