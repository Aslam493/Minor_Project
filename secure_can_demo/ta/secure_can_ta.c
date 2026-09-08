// SPDX-License-Identifier: BSD-2-Clause
#include <inttypes.h>
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#include <secure_can_ta.h>

TEE_Result TA_CreateEntryPoint(void)
{
	IMSG("Secure CAN demonstration TA created");
	return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void)
{
	IMSG("Secure CAN demonstration TA destroyed");
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types,
				    TEE_Param __unused params[4],
				    void __unused **session_context)
{
	const uint32_t expected = TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE,
			TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
			TEE_PARAM_TYPE_NONE);

	if (param_types != expected)
		return TEE_ERROR_BAD_PARAMETERS;

	IMSG("Secure CAN session opened");
	return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void __unused *session_context)
{
	IMSG("Secure CAN session closed");
}

static TEE_Result process_challenge(uint32_t param_types, TEE_Param params[4])
{
	const uint32_t expected = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INOUT,
			TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
			TEE_PARAM_TYPE_NONE);

	if (param_types != expected)
		return TEE_ERROR_BAD_PARAMETERS;

	IMSG("Received Normal World challenge 0x%08" PRIx32,
	     params[0].value.a);
	params[0].value.a ^= SECURE_CAN_MASK;
	params[0].value.b = SECURE_CAN_MARKER;
	IMSG("Returning Secure World response 0x%08" PRIx32,
	     params[0].value.a);
	return TEE_SUCCESS;
}

TEE_Result TA_InvokeCommandEntryPoint(void __unused *session_context,
				      uint32_t command_id,
				      uint32_t param_types,
				      TEE_Param params[4])
{
	if (command_id == TA_SECURE_CAN_CMD_PROCESS_CHALLENGE)
		return process_challenge(param_types, params);

	return TEE_ERROR_NOT_SUPPORTED;
}
