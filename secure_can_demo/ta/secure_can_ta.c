// SPDX-License-Identifier: BSD-2-Clause
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#include <secure_can_ta.h>

#define SECURE_CAN_KEY_LEN 32U

/* Fixed demo authentication key. It never leaves Secure World, which is the
 * property being demonstrated, but it is compiled into the TA rather than
 * provisioned securely, so this is a prototype, not a production key.
 */
static const uint8_t secure_can_demo_key[SECURE_CAN_KEY_LEN] = {
	0x4b, 0x1e, 0x9a, 0x77, 0x3c, 0xd2, 0x08, 0xf5,
	0x61, 0xae, 0x2f, 0x90, 0xc3, 0x14, 0x7b, 0xe6,
	0x82, 0x35, 0xda, 0x0c, 0x59, 0xf1, 0xa4, 0x6d,
	0x27, 0xbb, 0x93, 0x40, 0xe8, 0x1c, 0x66, 0xf9,
};

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

static TEE_Result compute_frame_tag(const struct secure_can_frame *frame,
				    uint8_t *tag, uint32_t *tag_len)
{
	TEE_ObjectHandle key_handle = TEE_HANDLE_NULL;
	TEE_OperationHandle op = TEE_HANDLE_NULL;
	TEE_Attribute attr;
	TEE_Result res;
	uint8_t msg[sizeof(frame->can_id) + SECURE_CAN_FRAME_DATA_LEN];

	memcpy(msg, &frame->can_id, sizeof(frame->can_id));
	memcpy(msg + sizeof(frame->can_id), frame->data,
	       SECURE_CAN_FRAME_DATA_LEN);

	res = TEE_AllocateTransientObject(TEE_TYPE_HMAC_SHA256,
					  SECURE_CAN_KEY_LEN * 8, &key_handle);
	if (res != TEE_SUCCESS)
		return res;

	TEE_InitRefAttribute(&attr, TEE_ATTR_SECRET_VALUE, secure_can_demo_key,
			     SECURE_CAN_KEY_LEN);
	res = TEE_PopulateTransientObject(key_handle, &attr, 1);
	if (res != TEE_SUCCESS)
		goto out;

	res = TEE_AllocateOperation(&op, TEE_ALG_HMAC_SHA256, TEE_MODE_MAC,
				    SECURE_CAN_KEY_LEN * 8);
	if (res != TEE_SUCCESS)
		goto out;

	res = TEE_SetOperationKey(op, key_handle);
	if (res != TEE_SUCCESS)
		goto out;

	TEE_MACInit(op, NULL, 0);
	res = TEE_MACComputeFinal(op, msg, sizeof(msg), tag, tag_len);

out:
	if (op != TEE_HANDLE_NULL)
		TEE_FreeOperation(op);
	if (key_handle != TEE_HANDLE_NULL)
		TEE_FreeTransientObject(key_handle);
	return res;
}

static TEE_Result process_frame(uint32_t param_types, TEE_Param params[4])
{
	const uint32_t expected = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
			TEE_PARAM_TYPE_MEMREF_OUTPUT, TEE_PARAM_TYPE_NONE,
			TEE_PARAM_TYPE_NONE);
	struct secure_can_frame frame;
	uint8_t full_tag[32]; /* HMAC-SHA256 output size */
	uint32_t full_tag_len = sizeof(full_tag);
	TEE_Result res;

	if (param_types != expected)
		return TEE_ERROR_BAD_PARAMETERS;

	if (params[0].memref.size != sizeof(frame))
		return TEE_ERROR_BAD_PARAMETERS;
	if (params[1].memref.size < SECURE_CAN_FRAME_TAG_LEN)
		return TEE_ERROR_SHORT_BUFFER;

	memcpy(&frame, params[0].memref.buffer, sizeof(frame));

	IMSG("Secure World received simulated CAN frame id=0x%03" PRIx32,
	     frame.can_id);

	res = compute_frame_tag(&frame, full_tag, &full_tag_len);
	if (res != TEE_SUCCESS) {
		EMSG("Secure World frame authentication failed: 0x%08" PRIx32,
		     res);
		return res;
	}

	memcpy(params[1].memref.buffer, full_tag, SECURE_CAN_FRAME_TAG_LEN);
	params[1].memref.size = SECURE_CAN_FRAME_TAG_LEN;

	IMSG("Secure World authenticated CAN frame id=0x%03" PRIx32
	     ", handing tag to Normal World", frame.can_id);
	return TEE_SUCCESS;
}

static TEE_Result classify_frame(uint32_t param_types, TEE_Param params[4])
{
	const uint32_t expected = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
			TEE_PARAM_TYPE_VALUE_OUTPUT, TEE_PARAM_TYPE_NONE,
			TEE_PARAM_TYPE_NONE);
	struct secure_can_frame frame;

	if (param_types != expected)
		return TEE_ERROR_BAD_PARAMETERS;
	if (params[0].memref.size != sizeof(frame))
		return TEE_ERROR_BAD_PARAMETERS;

	memcpy(&frame, params[0].memref.buffer, sizeof(frame));

	params[1].value.a = (frame.can_id <= SECURE_CAN_CRITICAL_ID_MAX) ?
			    SECURE_CAN_CLASS_SECURE : SECURE_CAN_CLASS_NORMAL;

	IMSG("Secure World classified CAN frame id=0x%03" PRIx32 " as %s",
	     frame.can_id, params[1].value.a == SECURE_CAN_CLASS_SECURE ?
	     "SECURE (route for authentication)" : "NORMAL (Normal World handles it)");
	return TEE_SUCCESS;
}

/* Constant-time so a timing side channel can't help a Normal World attacker
 * guess a valid tag one byte at a time.
 */
static bool tags_match(const uint8_t *a, const uint8_t *b, size_t len)
{
	uint8_t diff = 0;
	size_t i;

	for (i = 0; i < len; i++)
		diff |= a[i] ^ b[i];

	return diff == 0;
}

static TEE_Result verify_frame(uint32_t param_types, TEE_Param params[4])
{
	const uint32_t expected = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
			TEE_PARAM_TYPE_MEMREF_INPUT, TEE_PARAM_TYPE_VALUE_OUTPUT,
			TEE_PARAM_TYPE_NONE);
	struct secure_can_frame frame;
	uint8_t claimed_tag[SECURE_CAN_FRAME_TAG_LEN];
	uint8_t full_tag[32]; /* HMAC-SHA256 output size */
	uint32_t full_tag_len = sizeof(full_tag);
	TEE_Result res;

	if (param_types != expected)
		return TEE_ERROR_BAD_PARAMETERS;

	if (params[0].memref.size != sizeof(frame))
		return TEE_ERROR_BAD_PARAMETERS;
	if (params[1].memref.size != SECURE_CAN_FRAME_TAG_LEN)
		return TEE_ERROR_BAD_PARAMETERS;

	memcpy(&frame, params[0].memref.buffer, sizeof(frame));
	memcpy(claimed_tag, params[1].memref.buffer, SECURE_CAN_FRAME_TAG_LEN);

	res = compute_frame_tag(&frame, full_tag, &full_tag_len);
	if (res != TEE_SUCCESS) {
		EMSG("Secure World frame verification failed: 0x%08" PRIx32,
		     res);
		return res;
	}

	params[2].value.a = tags_match(claimed_tag, full_tag,
				       SECURE_CAN_FRAME_TAG_LEN) ? 1 : 0;

	IMSG("Secure World verified CAN frame id=0x%03" PRIx32 ": %s",
	     frame.can_id, params[2].value.a ? "AUTHENTIC" : "REJECTED");
	return TEE_SUCCESS;
}

TEE_Result TA_InvokeCommandEntryPoint(void __unused *session_context,
				      uint32_t command_id,
				      uint32_t param_types,
				      TEE_Param params[4])
{
	switch (command_id) {
	case TA_SECURE_CAN_CMD_PROCESS_CHALLENGE:
		return process_challenge(param_types, params);
	case TA_SECURE_CAN_CMD_PROCESS_FRAME:
		return process_frame(param_types, params);
	case TA_SECURE_CAN_CMD_VERIFY_FRAME:
		return verify_frame(param_types, params);
	case TA_SECURE_CAN_CMD_CLASSIFY_FRAME:
		return classify_frame(param_types, params);
	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}
}
