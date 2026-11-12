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
