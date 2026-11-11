// SPDX-License-Identifier: BSD-2-Clause
/*
 * Secure CAN Monitor: PTA glue (design 4k K1 and K9). Dispatch, the legacy-command policy and the mapping of
 * TEE_Param to the plain-C command logic in plat-rpi3/rpi3_can_monitor.c. It holds no monitor state of its own.
 *
 * Called by invoke_command() in rpi3_can_spi_poc.c, only in builds with CFG_RPI3_CAN_MONITOR=y. Dispatch order:
 *  (1) an id outside 0 to 23 returns NOT_SUPPORTED before any parameter check;
 *  (2) a refused legacy id returns NOT_SUPPORTED before any parameter check, plus a best-effort event;
 *  (3) ids 17 to 23 are handled here (exact param_types, then the logic);
 *  (4) everything else (including 1, 2, 12) is left to the unchanged legacy path (handled = false).
 */
#include <pta_rpi3_can_spi_poc.h>
#include <rpi3_can_monitor.h>

#define PT(a, b, c, d)	TEE_PARAM_TYPES(TEE_PARAM_TYPE_##a, TEE_PARAM_TYPE_##b, TEE_PARAM_TYPE_##c, TEE_PARAM_TYPE_##d)

static TEE_Result rpi3_mon_to_tee(enum rpi3_mon_res r)
{
	switch (r) {
	case RPI3_MON_R_OK:
		return TEE_SUCCESS;
	case RPI3_MON_R_BAD_PARAMETERS:
		return TEE_ERROR_BAD_PARAMETERS;
	case RPI3_MON_R_BAD_STATE:
		return TEE_ERROR_BAD_STATE;
	case RPI3_MON_R_SHORT_BUFFER:
		return TEE_ERROR_SHORT_BUFFER;
	case RPI3_MON_R_BUSY:
		return TEE_ERROR_BUSY;
	default:
		break;
	}
	return TEE_ERROR_BAD_STATE;
}

#ifdef CFG_RPI3_CAN_MONITOR_TEST_STATS
#define RPI3_MON_TEST_CMD_STATS	24	/* p0 MEMREF_OUTPUT (>= sizeof(struct rpi3_mon_tstats)) */
#define RPI3_MON_TEST_CMD_CTRL	25	/* p0 VALUE_INOUT: a op (0 reset, 1 hold request, 2 read), b ticks; p1 VALUE_INPUT: b high word; out a done count */

static TEE_Result rpi3_mon_pta_test_stats(uint32_t pt, TEE_Param params[TEE_NUM_PARAMS])
{
	size_t n;

	if (pt != PT(MEMREF_OUTPUT, NONE, NONE, NONE))
		return TEE_ERROR_BAD_PARAMETERS;
	n = rpi3_mon_ts_copy(params[0].memref.buffer, params[0].memref.size);
	if (!n) {
		params[0].memref.size = sizeof(struct rpi3_mon_tstats);
		return TEE_ERROR_SHORT_BUFFER;
	}
	params[0].memref.size = n;
	return TEE_SUCCESS;
}

static TEE_Result rpi3_mon_pta_test_ctrl(uint32_t pt, TEE_Param params[TEE_NUM_PARAMS])
{
	uint32_t done = 0;

	if (pt != PT(VALUE_INOUT, VALUE_INPUT, NONE, NONE))
		return TEE_ERROR_BAD_PARAMETERS;
	if (rpi3_mon_ts_ctrl(params[0].value.a, (uint64_t)params[0].value.b | ((uint64_t)params[1].value.a << 32), &done))
		return TEE_ERROR_BAD_PARAMETERS;
	params[0].value.a = done;
	return TEE_SUCCESS;
}
#endif

/* Slots 2 and 3 are the health pair in every monitor command (zero for every result except SUCCESS and BUSY). */
static void rpi3_mon_put_health(TEE_Param params[TEE_NUM_PARAMS], const struct rpi3_mon_health_out *h)
{
	params[2].value.a = h->p2a;
	params[2].value.b = h->p2b;
	params[3].value.a = h->p3a;
	params[3].value.b = h->p3b;
}

static bool rpi3_mon_has_values(enum rpi3_mon_res r)
{
	return r == RPI3_MON_R_OK || r == RPI3_MON_R_BUSY;
}

static TEE_Result rpi3_mon_pta_cmd17(uint32_t pt, TEE_Param params[TEE_NUM_PARAMS])
{
	struct rpi3_mon_health_out h;
	uint32_t v = 0;
	uint32_t d = 0;
	enum rpi3_mon_res r;

	if (pt != PT(VALUE_OUTPUT, MEMREF_INPUT, VALUE_OUTPUT, VALUE_OUTPUT))
		return TEE_ERROR_BAD_PARAMETERS;
	r = rpi3_mon_cmd17(params[1].memref.buffer, params[1].memref.size, &v, &d, &h);
	if (rpi3_mon_has_values(r)) {
		params[0].value.a = v;
		params[0].value.b = d;
	}
	rpi3_mon_put_health(params, &h);
	return rpi3_mon_to_tee(r);
}

static TEE_Result rpi3_mon_pta_cmd18(uint32_t pt, TEE_Param params[TEE_NUM_PARAMS])
{
	struct rpi3_mon_health_out h;
	uint32_t oa = 0;
	uint32_t ob = 0;
	size_t len;
	enum rpi3_mon_res r;

	if (pt != PT(VALUE_INOUT, MEMREF_OUTPUT, VALUE_OUTPUT, VALUE_OUTPUT))
		return TEE_ERROR_BAD_PARAMETERS;
	len = params[1].memref.size;
	r = rpi3_mon_cmd18(params[0].value.a, params[0].value.b, params[1].memref.buffer, &len, &oa, &ob, &h);
	if (rpi3_mon_has_values(r)) {
		params[0].value.a = oa;
		params[0].value.b = ob;
	}
	if (rpi3_mon_has_values(r) || r == RPI3_MON_R_SHORT_BUFFER)
		params[1].memref.size = len;
	rpi3_mon_put_health(params, &h);
	return rpi3_mon_to_tee(r);
}

static TEE_Result rpi3_mon_pta_cmd19(uint32_t pt, TEE_Param params[TEE_NUM_PARAMS])
{
	struct rpi3_mon_health_out h;
	uint32_t oa = 0;
	uint32_t ob = 0;
	enum rpi3_mon_res r;

	if (pt != PT(VALUE_INOUT, NONE, VALUE_OUTPUT, VALUE_OUTPUT))
		return TEE_ERROR_BAD_PARAMETERS;
	r = rpi3_mon_cmd19(params[0].value.a, params[0].value.b, &oa, &ob, &h);
	if (rpi3_mon_has_values(r)) {
		params[0].value.a = oa;
		params[0].value.b = ob;
	}
	rpi3_mon_put_health(params, &h);
	return rpi3_mon_to_tee(r);
}

static TEE_Result rpi3_mon_pta_cmd20(uint32_t pt, TEE_Param params[TEE_NUM_PARAMS])
{
	struct rpi3_mon_health_out h;
	uint32_t oa = 0;
	uint32_t ob = 0;
	size_t len;
	enum rpi3_mon_res r;

	if (pt != PT(VALUE_INOUT, MEMREF_OUTPUT, VALUE_OUTPUT, VALUE_OUTPUT))
		return TEE_ERROR_BAD_PARAMETERS;
	len = params[1].memref.size;
	r = rpi3_mon_cmd20(params[0].value.a, params[0].value.b, params[1].memref.buffer, &len, &oa, &ob, &h);
	if (rpi3_mon_has_values(r)) {
		params[0].value.a = oa;
		params[0].value.b = ob;
	}
	if (rpi3_mon_has_values(r) || r == RPI3_MON_R_SHORT_BUFFER)
		params[1].memref.size = len;
	rpi3_mon_put_health(params, &h);
	return rpi3_mon_to_tee(r);
}

static TEE_Result rpi3_mon_pta_cmd21(uint32_t pt, TEE_Param params[TEE_NUM_PARAMS])
{
	struct rpi3_mon_health_out h;
	uint32_t oa = 0;
	uint32_t ob = 0;
	enum rpi3_mon_res r;

	if (pt != PT(VALUE_INOUT, NONE, VALUE_OUTPUT, VALUE_OUTPUT))
		return TEE_ERROR_BAD_PARAMETERS;
	r = rpi3_mon_cmd21(params[0].value.a, params[0].value.b, &oa, &ob, &h);
	if (rpi3_mon_has_values(r)) {
		params[0].value.a = oa;
		params[0].value.b = ob;
	}
	rpi3_mon_put_health(params, &h);
	return rpi3_mon_to_tee(r);
}

static TEE_Result rpi3_mon_pta_cmd22(uint32_t pt, TEE_Param params[TEE_NUM_PARAMS])
{
	struct rpi3_mon_health_out h;
	uint32_t lo = 0;
	uint32_t hi = 0;
	uint32_t seq = 0;
	uint32_t st = 0;
	enum rpi3_mon_res r;

	if (pt != PT(VALUE_OUTPUT, VALUE_OUTPUT, VALUE_OUTPUT, VALUE_OUTPUT))
		return TEE_ERROR_BAD_PARAMETERS;
	r = rpi3_mon_cmd22(&lo, &hi, &seq, &st, &h);
	if (rpi3_mon_has_values(r)) {
		params[0].value.a = lo;
		params[0].value.b = hi;
		params[1].value.a = seq;
		params[1].value.b = st;
	}
	rpi3_mon_put_health(params, &h);
	return rpi3_mon_to_tee(r);
}

static TEE_Result rpi3_mon_pta_cmd23(uint32_t pt, TEE_Param params[TEE_NUM_PARAMS])
{
	struct rpi3_mon_health_out h;
	uint32_t oa = 0;
	uint32_t ob = 0;
	enum rpi3_mon_res r;

	if (pt != PT(VALUE_INOUT, VALUE_INPUT, VALUE_OUTPUT, VALUE_OUTPUT))
		return TEE_ERROR_BAD_PARAMETERS;
	r = rpi3_mon_cmd23(params[0].value.a, params[0].value.b, params[1].value.a, params[1].value.b, &oa, &ob, &h);
	if (rpi3_mon_has_values(r)) {
		params[0].value.a = oa;
		params[0].value.b = ob;
	}
	rpi3_mon_put_health(params, &h);
	return rpi3_mon_to_tee(r);
}

TEE_Result rpi3_mon_pta_invoke(uint32_t cmd_id, uint32_t param_types, TEE_Param params[TEE_NUM_PARAMS], bool *handled)
{
	*handled = true;

#ifdef CFG_RPI3_CAN_MONITOR_TEST_STATS
	/* TEST BUILDS ONLY: commands 24 (read statistics) and 25 (control) */
	if (cmd_id == RPI3_MON_TEST_CMD_STATS)
		return rpi3_mon_pta_test_stats(param_types, params);
	if (cmd_id == RPI3_MON_TEST_CMD_CTRL)
		return rpi3_mon_pta_test_ctrl(param_types, params);
#endif

	/* (1) the known set is 0 to 23 */
	if (cmd_id > PTA_RPI3_CAN_SPI_POC_CMD_MON_SUBMIT_HB_ANSWER)
		return TEE_ERROR_NOT_SUPPORTED;

	/* (3) the monitor commands */
	switch (cmd_id) {
	case PTA_RPI3_CAN_SPI_POC_CMD_MON_SUBMIT_FRAME:
		return rpi3_mon_pta_cmd17(param_types, params);
	case PTA_RPI3_CAN_SPI_POC_CMD_MON_GET_EVENTS:
		return rpi3_mon_pta_cmd18(param_types, params);
	case PTA_RPI3_CAN_SPI_POC_CMD_MON_ACK_EVENTS:
		return rpi3_mon_pta_cmd19(param_types, params);
	case PTA_RPI3_CAN_SPI_POC_CMD_MON_GET_STATUS:
		return rpi3_mon_pta_cmd20(param_types, params);
	case PTA_RPI3_CAN_SPI_POC_CMD_MON_SET_IDLE:
		return rpi3_mon_pta_cmd21(param_types, params);
	case PTA_RPI3_CAN_SPI_POC_CMD_MON_GET_NONCE:
		return rpi3_mon_pta_cmd22(param_types, params);
	case PTA_RPI3_CAN_SPI_POC_CMD_MON_SUBMIT_HB_ANSWER:
		return rpi3_mon_pta_cmd23(param_types, params);

	/* (2) refused in both profiles: raw SPI access, FIQ arming, transmit, Core 1 wake */
	case PTA_RPI3_CAN_SPI_POC_CMD_READ_OSC_REG:
	case PTA_RPI3_CAN_SPI_POC_CMD_ARM_FIQ:
	case PTA_RPI3_CAN_SPI_POC_CMD_READ_REG32:
	case PTA_RPI3_CAN_SPI_POC_CMD_TRANSMIT_TEST_FRAME:
	case PTA_RPI3_CAN_SPI_POC_CMD_WAKE_CORE1:
	case PTA_RPI3_CAN_SPI_POC_CMD_WRITE_REG32:
		rpi3_mon_note_legacy(cmd_id, true);
		return TEE_ERROR_NOT_SUPPORTED;

	/* chip A debug readers: refused in the hardened profile, allowed with an event in the debug profile */
	case PTA_RPI3_CAN_SPI_POC_CMD_GET_LAST_FRAME:
	case PTA_RPI3_CAN_SPI_POC_CMD_GET_DEBUG:
	case PTA_RPI3_CAN_SPI_POC_CMD_GET_RXIF_DEBUG:
	case PTA_RPI3_CAN_SPI_POC_CMD_GET_BDIAG1_DEBUG:
	case PTA_RPI3_CAN_SPI_POC_CMD_GET_TX_FAIL_DEBUG:
	case PTA_RPI3_CAN_SPI_POC_CMD_GET_GPIO_DEBUG:
	case PTA_RPI3_CAN_SPI_POC_CMD_GET_VALIDATION_DEBUG:
	case PTA_RPI3_CAN_SPI_POC_CMD_GET_TIMESTAMP_DEBUG:
		if (MON_PLAT_PROFILE_DEBUG) {
			rpi3_mon_note_legacy(cmd_id, false);
			*handled = false;
			return TEE_SUCCESS;
		}
		rpi3_mon_note_legacy(cmd_id, true);
		return TEE_ERROR_NOT_SUPPORTED;

	default:
		/* (4) 1, 2 and 12 (used by verify_boot) stay on the unchanged legacy path */
		*handled = false;
		return TEE_SUCCESS;
	}
}
