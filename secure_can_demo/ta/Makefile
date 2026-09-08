CFG_TEE_TA_LOG_LEVEL ?= 4
CFG_TA_OPTEE_CORE_API_COMPAT_1_1 := y
BINARY := abecf637-ca20-467e-8e6c-0b8779ba6b2f

-include $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk

ifeq ($(wildcard $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk),)
clean:
	@echo "TA development kit not found: $(TA_DEV_KIT_DIR)"
endif
