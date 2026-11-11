// SPDX-License-Identifier: BSD-2-Clause
/*
 * Phase 1 proof-of-concept for the project's Secure Peripheral Ownership
 * work (KNOWLEDGE.md, roadmap Step 14). Talks to the MCP2518FD CAN chip
 * on SPI0 chip-select 0 (can0) directly from OP-TEE core, via a
 * from-scratch polled BCM2835 SPI0 driver - completely independent of
 * Linux's spi_bcm2835/mcp251xfd drivers. No interrupt/GIC routing yet;
 * that is a separate, higher-risk phase (see KNOWLEDGE.md, Incident 4).
 *
 * Assumes Linux has already configured the SPI0 controller's own pin
 * mux (ALT0 on GPIO 7-11) via its normal boot-time probe of the `spi0`
 * device-tree node - this PoC only takes over the register-level
 * transfer itself, not pin muxing, keeping its scope to exactly what
 * Step 14 asks to prove first.
 */

#include <io.h>
#include <kernel/pseudo_ta.h>
#include <mm/core_memprot.h>
#include <platform_config.h>
#include <string.h>

/* Defined in rpi3_can_fiq.c (Phase 2, roadmap Steps 15-17) */
uint32_t rpi3_can_fiq_count(void);
uint32_t rpi3_can_fiq_write_reg32(uint32_t addr, uint32_t value);
uint32_t rpi3_can_fiq_get_debug(uint32_t *fifo_index, uint32_t *fifo_addr);
uint32_t rpi3_can_fiq_get_last_frame(uint32_t *id, uint8_t data[8]);
void rpi3_can_fiq_arm_gpio(void);
uint32_t rpi3_can_fiq_transmit(uint32_t id, const uint8_t data[8]);
void rpi3_can_fiq_get_rxif_debug(uint32_t *rxif_mask, uint32_t *int_reg);
uint32_t rpi3_can_fiq_get_bdiag1_debug(void);
uint32_t rpi3_can_fiq_get_tx_fail_point(void);
uint32_t rpi3_can_fiq_get_gpio_debug(uint32_t *gpeds0);
uint32_t rpi3_can_fiq_get_validation_debug(uint32_t *last_counter,
					   uint32_t *checked_id,
					   uint32_t *checked_counter,
					   uint32_t *dropped_count);
void rpi3_can_fiq_get_timestamp_debug(uint32_t *last_timestamp, uint32_t *tbc,
				      uint32_t *tscon, uint32_t *packet_epoch,
				      uint32_t *current_epoch);

/* Defined in rpi3_can_server.c (Plan A hard core-pinning, Test A) */
uint64_t rpi3_can_server_get_heartbeat(void);
int32_t rpi3_can_server_wake_core1(void);
void rpi3_route_fiq_to_primary_core(vaddr_t local);
#include <pta_rpi3_can_spi_poc.h>
#include <trace.h>

#define PTA_NAME "rpi3_can_spi_poc.pta"

/* BCM2835 SPI0 controller register offsets */
#define SPI_CS   0x00
#define SPI_FIFO 0x04
#define SPI_CLK  0x08

#define SPI_CS_CS_MASK   GENMASK_32(1, 0) /* 00 = CE0 */
#define SPI_CS_CLEAR_TX  BIT32(4)
#define SPI_CS_CLEAR_RX  BIT32(5)
#define SPI_CS_TA        BIT32(7)
#define SPI_CS_DONE      BIT32(16)
#define SPI_CS_RXD       BIT32(17)
#define SPI_CS_TXD       BIT32(18)

/* Conservative divider: keeps the SPI clock safely under the chip's
 * 20 MHz max regardless of the exact VPU core clock rate.
 */
#define SPI_CLK_DIVIDER 32

/* BCM2835 GPIO controller registers/pin. This board's DTB gives spi0 a
 * `cs-gpios` property (GPIO 8, active-low, for CS0) - Linux drives chip
 * select as a plain GPIO, not the SPI controller's native CS field, so
 * this code must do the same or the chip's CS pin is never asserted.
 */
#define GPIO_GPFSEL0 0x00
#define GPIO_GPSET0  0x1c
#define GPIO_GPCLR0  0x28
#define CAN0_CS_GPIO 8U

/* MCP251XFD SPI protocol (see Linux drivers/net/can/spi/mcp251xfd for
 * the same constants, used here only as a specification reference -
 * this is independent, from-scratch code, not a port of that driver).
 */
#define MCP251XFD_INSTRUCTION_READ 0x3000
#define MCP251XFD_REG_OSC          0x0e00

#define SPI_POLL_TIMEOUT_LOOPS 1000000

static vaddr_t spi_base(void)
{
	return core_mmu_get_va(SPI0_BASE, MEM_AREA_IO_SEC, SPI0_REG_SIZE);
}

static vaddr_t gpio_base(void)
{
	return core_mmu_get_va(GPIO_BASE, MEM_AREA_IO_SEC, GPIO_REG_SIZE);
}

/* Ensures GPIO 8 is an output, then drives it (active-low CS: low =
 * asserted/selected, high = deasserted).
 */
static void can0_cs_set(bool assert)
{
	vaddr_t base = gpio_base();
	uint32_t fsel;

	/* GPFSEL0 covers pins 0-9, 3 bits each; pin 8 -> bits 26:24, 001 = output */
	fsel = io_read32(base + GPIO_GPFSEL0);
	fsel &= ~GENMASK_32(26, 24);
	fsel |= (0x1 << 24);
	io_write32(base + GPIO_GPFSEL0, fsel);

	if (assert)
		io_write32(base + GPIO_GPCLR0, BIT32(CAN0_CS_GPIO));
	else
		io_write32(base + GPIO_GPSET0, BIT32(CAN0_CS_GPIO));
}

static TEE_Result spi_wait_bit(vaddr_t base, uint32_t bit)
{
	unsigned int i;

	for (i = 0; i < SPI_POLL_TIMEOUT_LOOPS; i++) {
		if (io_read32(base + SPI_CS) & bit)
			return TEE_SUCCESS;
	}
	return TEE_ERROR_BUSY;
}

/* Polled full-duplex transfer: writes tx[0..len) and reads back rx[0..len)
 * on SPI0/CE0, one byte at a time. len is small here (6 bytes), so the
 * 16-byte hardware FIFO never risks overflow - no need for bulk FIFO
 * handling in this proof-of-concept.
 */
static TEE_Result spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
	vaddr_t base = spi_base();
	TEE_Result res = TEE_SUCCESS;
	size_t i = 0;

	/* Mode 0 (CPOL=0, CPHA=0), clear both FIFOs before starting */
	io_write32(base + SPI_CS, SPI_CS_CLEAR_TX | SPI_CS_CLEAR_RX);
	io_write32(base + SPI_CLK, SPI_CLK_DIVIDER);

	/* Chip select is a plain GPIO on this board (see can0_cs_set), not
	 * the SPI controller's native CS field - assert it ourselves.
	 */
	can0_cs_set(true);

	/* Start the transfer (drives the clock/data shifting; CS itself is
	 * handled above, independent of this bit on this board)
	 */
	io_write32(base + SPI_CS, SPI_CS_TA);

	for (i = 0; i < len; i++) {
		res = spi_wait_bit(base, SPI_CS_TXD);
		if (res)
			goto out;
		io_write32(base + SPI_FIFO, tx[i]);

		res = spi_wait_bit(base, SPI_CS_RXD);
		if (res)
			goto out;
		rx[i] = io_read32(base + SPI_FIFO) & 0xff;
	}

	res = spi_wait_bit(base, SPI_CS_DONE);
out:
	io_write32(base + SPI_CS, 0);
	can0_cs_set(false);
	return res;
}

static TEE_Result read_register32(uint16_t addr, uint32_t *value)
{
	uint8_t tx[6] = { 0 };
	uint8_t rx[6] = { 0 };
	uint16_t cmd = MCP251XFD_INSTRUCTION_READ | addr;
	TEE_Result res = TEE_SUCCESS;

	tx[0] = (cmd >> 8) & 0xff;
	tx[1] = cmd & 0xff;
	/* tx[2..5] stay zero: dummy bytes to clock out the register data */

	res = spi_transfer(tx, rx, sizeof(tx));
	if (res) {
		EMSG(PTA_NAME ": SPI transfer failed: %#" PRIx32, res);
		return res;
	}

	/* Register data begins after the 2-byte command echo, same framing
	 * as the already-verified Linux driver uses for this chip.
	 */
	*value = (uint32_t)rx[2] | ((uint32_t)rx[3] << 8) |
		 ((uint32_t)rx[4] << 16) | ((uint32_t)rx[5] << 24);
	return TEE_SUCCESS;
}

static TEE_Result invoke_command(void *sess_ctx __unused, uint32_t cmd_id,
				 uint32_t param_types,
				 TEE_Param params[TEE_NUM_PARAMS])
{
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE);
	uint32_t value = 0;
	TEE_Result res = TEE_SUCCESS;

	if (cmd_id == PTA_RPI3_CAN_SPI_POC_CMD_GET_LAST_FRAME) {
		uint32_t exp_pt_frame = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT,
							TEE_PARAM_TYPE_MEMREF_OUTPUT,
							TEE_PARAM_TYPE_NONE,
							TEE_PARAM_TYPE_NONE);
		uint32_t id = 0;
		uint8_t data[8] = { 0 };
		uint32_t got;

		if (param_types != exp_pt_frame)
			return TEE_ERROR_BAD_PARAMETERS;
		if (params[1].memref.size < sizeof(data))
			return TEE_ERROR_SHORT_BUFFER;

		got = rpi3_can_fiq_get_last_frame(&id, data);
		params[0].value.a = got;
		params[0].value.b = id;
		if (got)
			memcpy(params[1].memref.buffer, data, sizeof(data));
		return TEE_SUCCESS;
	}

	if (cmd_id == PTA_RPI3_CAN_SPI_POC_CMD_GET_VALIDATION_DEBUG) {
		uint32_t exp_pt_val = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT,
						      TEE_PARAM_TYPE_MEMREF_OUTPUT,
						      TEE_PARAM_TYPE_NONE,
						      TEE_PARAM_TYPE_NONE);
		uint32_t last_counter = 0;
		uint32_t checked_id = 0;
		uint32_t checked_counter = 0;
		uint32_t dropped_count = 0;
		uint32_t status;
		uint8_t out[12];

		if (param_types != exp_pt_val)
			return TEE_ERROR_BAD_PARAMETERS;
		if (params[1].memref.size < sizeof(out))
			return TEE_ERROR_SHORT_BUFFER;

		status = rpi3_can_fiq_get_validation_debug(&last_counter,
							   &checked_id,
							   &checked_counter,
							   &dropped_count);
		params[0].value.a = status;
		params[0].value.b = last_counter;

		out[0] = (uint8_t)checked_id;
		out[1] = (uint8_t)(checked_id >> 8);
		out[2] = (uint8_t)(checked_id >> 16);
		out[3] = (uint8_t)(checked_id >> 24);
		out[4] = (uint8_t)checked_counter;
		out[5] = (uint8_t)(checked_counter >> 8);
		out[6] = (uint8_t)(checked_counter >> 16);
		out[7] = (uint8_t)(checked_counter >> 24);
		out[8] = (uint8_t)dropped_count;
		out[9] = (uint8_t)(dropped_count >> 8);
		out[10] = (uint8_t)(dropped_count >> 16);
		out[11] = (uint8_t)(dropped_count >> 24);
		memcpy(params[1].memref.buffer, out, sizeof(out));
		return TEE_SUCCESS;
	}

	if (cmd_id == PTA_RPI3_CAN_SPI_POC_CMD_GET_TIMESTAMP_DEBUG) {
		uint32_t exp_pt_ts = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT,
						     TEE_PARAM_TYPE_MEMREF_OUTPUT,
						     TEE_PARAM_TYPE_NONE,
						     TEE_PARAM_TYPE_NONE);
		uint32_t last_timestamp = 0;
		uint32_t tbc = 0;
		uint32_t tscon = 0;
		uint32_t packet_epoch = 0;
		uint32_t current_epoch = 0;
		uint8_t out_tscon[12];

		if (param_types != exp_pt_ts)
			return TEE_ERROR_BAD_PARAMETERS;
		if (params[1].memref.size < sizeof(out_tscon))
			return TEE_ERROR_SHORT_BUFFER;

		rpi3_can_fiq_get_timestamp_debug(&last_timestamp, &tbc, &tscon,
						 &packet_epoch, &current_epoch);
		params[0].value.a = last_timestamp;
		params[0].value.b = tbc;

		out_tscon[0] = (uint8_t)tscon;
		out_tscon[1] = (uint8_t)(tscon >> 8);
		out_tscon[2] = (uint8_t)(tscon >> 16);
		out_tscon[3] = (uint8_t)(tscon >> 24);
		out_tscon[4] = (uint8_t)packet_epoch;
		out_tscon[5] = (uint8_t)(packet_epoch >> 8);
		out_tscon[6] = (uint8_t)(packet_epoch >> 16);
		out_tscon[7] = (uint8_t)(packet_epoch >> 24);
		out_tscon[8] = (uint8_t)current_epoch;
		out_tscon[9] = (uint8_t)(current_epoch >> 8);
		out_tscon[10] = (uint8_t)(current_epoch >> 16);
		out_tscon[11] = (uint8_t)(current_epoch >> 24);
		memcpy(params[1].memref.buffer, out_tscon, sizeof(out_tscon));
		return TEE_SUCCESS;
	}

	if (cmd_id == PTA_RPI3_CAN_SPI_POC_CMD_READ_REG32) {
		uint32_t exp_pt_reg = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INOUT,
						      TEE_PARAM_TYPE_NONE,
						      TEE_PARAM_TYPE_NONE,
						      TEE_PARAM_TYPE_NONE);
		uint16_t addr;
		uint32_t reg_value = 0;

		if (param_types != exp_pt_reg)
			return TEE_ERROR_BAD_PARAMETERS;

		addr = (uint16_t)params[0].value.a;
		res = read_register32(addr, &reg_value);
		if (res)
			return res;
		params[0].value.a = addr;
		params[0].value.b = reg_value;
		return TEE_SUCCESS;
	}

	if (cmd_id == PTA_RPI3_CAN_SPI_POC_CMD_WRITE_REG32) {
		uint32_t exp_pt_wreg = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INOUT,
						       TEE_PARAM_TYPE_NONE,
						       TEE_PARAM_TYPE_NONE,
						       TEE_PARAM_TYPE_NONE);
		uint32_t addr;
		uint32_t wvalue;
		uint32_t ok;

		if (param_types != exp_pt_wreg)
			return TEE_ERROR_BAD_PARAMETERS;

		addr = params[0].value.a;
		wvalue = params[0].value.b;
		ok = rpi3_can_fiq_write_reg32(addr, wvalue);
		params[0].value.a = ok;
		return TEE_SUCCESS;
	}

	if (cmd_id == PTA_RPI3_CAN_SPI_POC_CMD_TRANSMIT_TEST_FRAME) {
		/* INOUT: params[0].a is the CAN ID (in), params[0].b is the
		 * success/fail result (out) - both directions on the same
		 * slot, so INPUT alone is wrong here (see main.c's comment
		 * on this same command for what that bug actually caused).
		 */
		uint32_t exp_pt_tx = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INOUT,
						     TEE_PARAM_TYPE_MEMREF_INPUT,
						     TEE_PARAM_TYPE_NONE,
						     TEE_PARAM_TYPE_NONE);
		uint32_t id;
		uint32_t ok;

		if (param_types != exp_pt_tx)
			return TEE_ERROR_BAD_PARAMETERS;
		if (params[1].memref.size < 8)
			return TEE_ERROR_BAD_PARAMETERS;

		id = params[0].value.a;
		ok = rpi3_can_fiq_transmit(id, (const uint8_t *)params[1].memref.buffer);
		params[0].value.b = ok;
		return TEE_SUCCESS;
	}

	if (param_types != exp_pt)
		return TEE_ERROR_BAD_PARAMETERS;

	switch (cmd_id) {
	case PTA_RPI3_CAN_SPI_POC_CMD_READ_OSC_REG:
		res = read_register32(MCP251XFD_REG_OSC, &value);
		if (res)
			return res;
		IMSG(PTA_NAME ": read OSC register over real SPI0: %#" PRIx32,
		     value);
		break;
	case PTA_RPI3_CAN_SPI_POC_CMD_GET_FIQ_COUNT:
		value = rpi3_can_fiq_count();
		break;
	case PTA_RPI3_CAN_SPI_POC_CMD_READ_FIQ_CONTROL:
		value = io_read32(core_mmu_get_va(VC_INTC_BASE, MEM_AREA_IO_SEC,
						  VC_INTC_REG_SIZE) + 0x0c);
		break;
	case PTA_RPI3_CAN_SPI_POC_CMD_ARM_FIQ: {
		vaddr_t local = core_mmu_get_va(ARM_LOCAL_BASE, MEM_AREA_IO_SEC,
						ARM_LOCAL_REG_SIZE);
		vaddr_t intc = core_mmu_get_va(VC_INTC_BASE, MEM_AREA_IO_SEC,
					       VC_INTC_REG_SIZE);

		/* Was its own independent copy of "always route FIQ to core
		 * 0" - now shares rpi3_can_fiq.c's single implementation, per
		 * the same fix applied there (see that function's comment).
		 */
		rpi3_route_fiq_to_primary_core(local);
		/* Fallback re-arm of GPIO 25's level-detect: the driver_init()
		 * call already does this, but that context has already proven
		 * unreliable once on this platform (see this command's own
		 * FIQ_CONTROL write below, which exists for the same reason) -
		 * calling this again here is idempotent and cheap.
		 */
		rpi3_can_fiq_arm_gpio();
		io_write32(intc + VC_INTC_FIQ_CONTROL_OFFSET,
			  VC_INTC_FIQ_ENABLE | CAN0_GPIO_FIQ_SOURCE);
		value = io_read32(intc + VC_INTC_FIQ_CONTROL_OFFSET);
		IMSG(PTA_NAME ": armed FIQ routing, FIQ_CONTROL now %#" PRIx32,
		     value);
		break;
	}
	case PTA_RPI3_CAN_SPI_POC_CMD_GET_DEBUG: {
		uint32_t fifo_index = 0;
		uint32_t fifo_addr = 0;

		rpi3_can_fiq_get_debug(&fifo_index, &fifo_addr);
		params[0].value.a = fifo_index;
		params[0].value.b = fifo_addr;
		return TEE_SUCCESS;
	}
	case PTA_RPI3_CAN_SPI_POC_CMD_GET_RXIF_DEBUG: {
		uint32_t rxif_mask = 0;
		uint32_t int_reg = 0;

		rpi3_can_fiq_get_rxif_debug(&rxif_mask, &int_reg);
		params[0].value.a = rxif_mask;
		params[0].value.b = int_reg;
		return TEE_SUCCESS;
	}
	case PTA_RPI3_CAN_SPI_POC_CMD_GET_BDIAG1_DEBUG: {
		params[0].value.a = rpi3_can_fiq_get_bdiag1_debug();
		params[0].value.b = 0;
		return TEE_SUCCESS;
	}
	case PTA_RPI3_CAN_SPI_POC_CMD_GET_TX_FAIL_DEBUG: {
		params[0].value.a = rpi3_can_fiq_get_tx_fail_point();
		params[0].value.b = 0;
		return TEE_SUCCESS;
	}
	case PTA_RPI3_CAN_SPI_POC_CMD_GET_GPIO_DEBUG: {
		uint32_t gpeds0 = 0;
		uint32_t level = rpi3_can_fiq_get_gpio_debug(&gpeds0);

		params[0].value.a = level;
		params[0].value.b = gpeds0;
		return TEE_SUCCESS;
	}
	case PTA_RPI3_CAN_SPI_POC_CMD_GET_CORE1_HEARTBEAT: {
		uint64_t heartbeat = rpi3_can_server_get_heartbeat();

		params[0].value.a = (uint32_t)heartbeat;
		params[0].value.b = (uint32_t)(heartbeat >> 32);
		return TEE_SUCCESS;
	}
	case PTA_RPI3_CAN_SPI_POC_CMD_WAKE_CORE1:
		params[0].value.a = (uint32_t)rpi3_can_server_wake_core1();
		params[0].value.b = 0;
		return TEE_SUCCESS;
	default:
		return TEE_ERROR_NOT_SUPPORTED;
	}

	params[0].value.a = value;
	params[0].value.b = 0;
	return TEE_SUCCESS;
}

pseudo_ta_register(.uuid = PTA_RPI3_CAN_SPI_POC_UUID, .name = PTA_NAME,
		   .flags = PTA_DEFAULT_FLAGS,
		   .invoke_command_entry_point = invoke_command);
