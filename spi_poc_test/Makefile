CC := $(CROSS_COMPILE)gcc

BINARY := spi_poc_test
OBJS := main.o
CFLAGS += -Wall -Wextra -Werror -I$(TEEC_EXPORT)/include -I$(TA_DEV_KIT_DIR)/host_include
LDADD += -L$(TEEC_EXPORT)/lib -lteec

.PHONY: all clean
all: $(BINARY)

$(BINARY): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDADD)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(BINARY)
