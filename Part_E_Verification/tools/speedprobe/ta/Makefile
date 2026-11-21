CFG_TEE_TA_LOG_LEVEL ?= 1
CFG_TA_OPTEE_CORE_API_COMPAT_1_1=y
BINARY=5a3f9d10-7c2b-4e61-9b1a-3d0e5f7a2c88
-include $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk
ifeq ($(wildcard $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk), )
clean:
	@echo 'Note: $$(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk not found, cannot clean TA'
endif
