# Host-only reference model of the Component 2.2 synchronization logic. Touches no OP-TEE source and no board.
CC ?= gcc
CFLAGS = -std=c11 -Wall -Wextra -O1 -g -fsanitize=address,undefined
test: test_sync
	./test_sync
test_sync: mon_model.c test_sync.c mon_model.h mon_plat.h
	$(CC) $(CFLAGS) mon_model.c test_sync.c -o test_sync
mutate:
	python3 mutation_test.py
clean:
	rm -f test_sync
