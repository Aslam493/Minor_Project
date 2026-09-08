export V ?= 0

HOST_CROSS_COMPILE ?= $(CROSS_COMPILE)
TA_CROSS_COMPILE ?= $(CROSS_COMPILE)

.PHONY: all host ta clean
all: host ta

host:
	$(MAKE) -C host CROSS_COMPILE="$(HOST_CROSS_COMPILE)" TEEC_EXPORT="$(TEEC_EXPORT)"

ta:
	$(MAKE) -C ta CROSS_COMPILE="$(TA_CROSS_COMPILE)" TA_DEV_KIT_DIR="$(TA_DEV_KIT_DIR)"

clean:
	$(MAKE) -C host clean
	$(MAKE) -C ta clean TA_DEV_KIT_DIR="$(TA_DEV_KIT_DIR)"
