# Host harness for the REAL monitor sources: harness.c includes plat-rpi3/rpi3_can_monitor.c, and links the REAL PTA glue
# core/pta/rpi3_can_monitor_pta.c. The mock directory is searched first so its header shadows the OP-TEE bindings header.
# 'strict' compiles the two real source files on their own with -Wall -Wextra -Werror; only the test code is exempt from
# -Wmisleading-indentation (the tests are written as compact one-liners).
OS := $(abspath ../../../optee_full/optee_os)
PLAT := $(OS)/core/arch/arm/plat-rpi3
CC ?= gcc
INC = -I mock -I $(PLAT) -I $(OS)/lib/libutee/include -I .
SAN = -fsanitize=address,undefined -fno-omit-frame-pointer
STRICT = -std=gnu11 -Wall -Wextra -Werror -O1 -g $(SAN) -pthread $(INC)
test: strict harness
	./harness
strict:
	$(CC) $(STRICT) -c $(PLAT)/rpi3_can_monitor.c -o /dev/null
	$(CC) $(STRICT) -c $(OS)/core/pta/rpi3_can_monitor_pta.c -o /dev/null
harness: harness.c mock_platform.c vectors.h $(PLAT)/rpi3_can_monitor.c $(PLAT)/rpi3_can_monitor.h $(PLAT)/rpi3_can_monitor_keys.h $(OS)/core/pta/rpi3_can_monitor_pta.c mock/rpi3_can_monitor_plat.h
	$(CC) $(STRICT) -Wno-misleading-indentation harness.c mock_platform.c $(OS)/core/pta/rpi3_can_monitor_pta.c -o harness
harness_ts: harness.c mock_platform.c vectors.h $(PLAT)/rpi3_can_monitor.c $(PLAT)/rpi3_can_monitor.h $(OS)/core/pta/rpi3_can_monitor_pta.c mock/rpi3_can_monitor_plat.h
	$(CC) $(STRICT) -Wno-misleading-indentation -DCFG_RPI3_CAN_MONITOR_TEST_STATS harness.c mock_platform.c $(OS)/core/pta/rpi3_can_monitor_pta.c -o harness_ts
strict_ts:
	$(CC) $(STRICT) -DCFG_RPI3_CAN_MONITOR_TEST_STATS -c $(PLAT)/rpi3_can_monitor.c -o /dev/null
	$(CC) $(STRICT) -DCFG_RPI3_CAN_MONITOR_TEST_STATS -c $(OS)/core/pta/rpi3_can_monitor_pta.c -o /dev/null
test_ts: strict_ts harness_ts
	./harness_ts
clean:
	rm -f harness
