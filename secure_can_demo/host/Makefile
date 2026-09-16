CC := $(CROSS_COMPILE)gcc

BINARY := secure_can_demo
OBJS := main.o
CFLAGS += -Wall -Wextra -Werror -I../ta/include -I$(TEEC_EXPORT)/include
LDADD += -L$(TEEC_EXPORT)/lib -lteec -lpthread

.PHONY: all clean
all: $(BINARY)

$(BINARY): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDADD)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(BINARY)
