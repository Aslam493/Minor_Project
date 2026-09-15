// SPDX-License-Identifier: BSD-2-Clause
/*
 * Secure physical timer support for the Raspberry Pi 3 dummy CAN prototype.
 */
#include <io.h>
#include <initcall.h>
#include <kernel/boot.h>
#include <kernel/callout.h>
#include <kernel/interrupt.h>
#include <kernel/misc.h>
#include <kernel/panic.h>
#include <kernel/pseudo_ta.h>
#include <kernel/spinlock.h>
#include <kernel/timer.h>
#include <mm/core_memprot.h>
#include <platform_config.h>
#include <stdint.h>
#include <tee_api_defines.h>
#include <tee_api_types.h>
#include <trace.h>
#include <util.h>

#define RPI3_LOCAL_TIMER_INT_CONTROL0 0x40
#define RPI3_LOCAL_IRQ_PENDING0       0x60
#define RPI3_LOCAL_CNTPSIRQ           0
#define RPI3_LOCAL_TIMER_IRQ_BIT      BIT(0)
#define RPI3_DUMMY_TIMER_CORE         1
#define RPI3_DUMMY_TIMER_PERIOD_MS    100

#define PTA_SECURE_CAN_PHASE_A_UUID \
	{ 0x7cb1a48e, 0x3b5f, 0x4b73, \
	  { 0x91, 0xe8, 0x73, 0x31, 0xc4, 0x7a, 0xd2, 0x10 } }
#define PTA_SECURE_CAN_PHASE_A_GET_STATUS 0
#define PTA_SECURE_CAN_PHASE_A_ARM_TIMER  1

register_phys_mem_pgdir(MEM_AREA_IO_SEC, RPI3_LOCAL_INTC_BASE,
			RPI3_LOCAL_INTC_SIZE);

static struct itr_chip rpi3_timer_chip;
static vaddr_t intc_base;
static struct callout dummy_counter_callout;
static unsigned int counter_lock = SPINLOCK_UNLOCK;
static uint32_t dummy_counter;
static uint32_t last_handler_core = UINT32_MAX;
static bool timer_initialized;

static vaddr_t core_reg(size_t offset)
{
	return intc_base + offset + sizeof(uint32_t) * get_core_pos();
}

static void timer_irq_configure(struct itr_chip *chip __unused,
				size_t it, uint32_t type __unused,
				uint32_t prio __unused)
{
	assert(it == RPI3_LOCAL_CNTPSIRQ);
}

static void timer_irq_unmask(struct itr_chip *chip __unused, size_t it)
{
	vaddr_t reg = core_reg(RPI3_LOCAL_TIMER_INT_CONTROL0);
	uint32_t value = 0;

	assert(it == RPI3_LOCAL_CNTPSIRQ);
	value = io_read32(reg);
	io_write32(reg, value | RPI3_LOCAL_TIMER_IRQ_BIT);
}

static void timer_irq_mask(struct itr_chip *chip __unused, size_t it)
{
	vaddr_t reg = core_reg(RPI3_LOCAL_TIMER_INT_CONTROL0);
	uint32_t value = 0;

	assert(it == RPI3_LOCAL_CNTPSIRQ);
	value = io_read32(reg);
	io_write32(reg, value & ~RPI3_LOCAL_TIMER_IRQ_BIT);
}

static const struct itr_ops timer_irq_ops = {
	.configure = timer_irq_configure,
	.enable = timer_irq_unmask,
	.disable = timer_irq_mask,
	.mask = timer_irq_mask,
	.unmask = timer_irq_unmask,
};

static bool dummy_counter_tick(struct callout *co __unused)
{
	uint32_t exceptions = cpu_spin_lock_xsave(&counter_lock);

	dummy_counter++;
	last_handler_core = get_core_pos();
	cpu_spin_unlock_xrestore(&counter_lock, exceptions);

	return true;
}
DECLARE_KEEP_PAGER(dummy_counter_tick);

static TEE_Result register_dummy_counter(void)
{
	callout_add(&dummy_counter_callout, dummy_counter_tick,
		    RPI3_DUMMY_TIMER_PERIOD_MS);
	return TEE_SUCCESS;
}
nex_early_init(register_dummy_counter);

void boot_primary_init_intc(void)
{
	intc_base = (vaddr_t)phys_to_virt(RPI3_LOCAL_INTC_BASE,
					 MEM_AREA_IO_SEC,
					 RPI3_LOCAL_INTC_SIZE);
	if (!intc_base)
		panic();

	rpi3_timer_chip = (struct itr_chip){
		.ops = &timer_irq_ops,
		.name = "bcm2836-secure-timer",
	};
	interrupt_main_init(&rpi3_timer_chip);
}

void boot_secondary_init_intc(void)
{
	/* Timer activation is deliberately deferred until Linux is stable. */
}

void interrupt_main_handler(void)
{
	uint32_t pending = io_read32(core_reg(RPI3_LOCAL_IRQ_PENDING0));

	if (pending & BIT(RPI3_LOCAL_CNTPSIRQ)) {
		timer_irq_mask(&rpi3_timer_chip, RPI3_LOCAL_CNTPSIRQ);
		interrupt_call_handlers(&rpi3_timer_chip,
		/* One-shot diagnostic: keep source masked after first event. */
					RPI3_LOCAL_CNTPSIRQ);
		return;
	}

	panic("Unexpected Raspberry Pi 3 secure interrupt");
}

static TEE_Result get_phase_a_status(uint32_t param_types,
				     TEE_Param params[TEE_NUM_PARAMS])
{
	const uint32_t expected =
		TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT,
				TEE_PARAM_TYPE_VALUE_OUTPUT,
				TEE_PARAM_TYPE_NONE,
				TEE_PARAM_TYPE_NONE);
	uint32_t exceptions = 0;

	if (param_types != expected)
		return TEE_ERROR_BAD_PARAMETERS;

	exceptions = cpu_spin_lock_xsave(&counter_lock);
	params[0].value.a = dummy_counter;
	params[0].value.b = last_handler_core;
	params[1].value.a = RPI3_DUMMY_TIMER_CORE;
	params[1].value.b = timer_initialized;
	cpu_spin_unlock_xrestore(&counter_lock, exceptions);

	return TEE_SUCCESS;
}

static TEE_Result arm_phase_a_timer(uint32_t param_types)
{
	uint32_t exceptions = 0;

	if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE,
					   TEE_PARAM_TYPE_NONE,
					   TEE_PARAM_TYPE_NONE,
					   TEE_PARAM_TYPE_NONE))
		return TEE_ERROR_BAD_PARAMETERS;
	if (get_core_pos() != RPI3_DUMMY_TIMER_CORE)
		return TEE_ERROR_BAD_STATE;

	exceptions = cpu_spin_lock_xsave(&counter_lock);
	if (timer_initialized) {
		cpu_spin_unlock_xrestore(&counter_lock, exceptions);
		return TEE_SUCCESS;
	}
	cpu_spin_unlock_xrestore(&counter_lock, exceptions);

	timer_init_callout_service(&rpi3_timer_chip, RPI3_LOCAL_CNTPSIRQ);
	exceptions = cpu_spin_lock_xsave(&counter_lock);
	timer_initialized = true;
	cpu_spin_unlock_xrestore(&counter_lock, exceptions);
	IMSG("Phase A secure timer armed at runtime on core %u at %u ms",
	     RPI3_DUMMY_TIMER_CORE, RPI3_DUMMY_TIMER_PERIOD_MS);
	return TEE_SUCCESS;
}

static TEE_Result phase_a_invoke(void *session __unused, uint32_t command,
				 uint32_t param_types,
				 TEE_Param params[TEE_NUM_PARAMS])
{
	if (command == PTA_SECURE_CAN_PHASE_A_GET_STATUS)
		return get_phase_a_status(param_types, params);
	if (command == PTA_SECURE_CAN_PHASE_A_ARM_TIMER)
		return arm_phase_a_timer(param_types);

	return TEE_ERROR_NOT_SUPPORTED;
}

pseudo_ta_register(.uuid = PTA_SECURE_CAN_PHASE_A_UUID,
		   .name = "secure_can.phase_a",
		   .flags = PTA_DEFAULT_FLAGS | TA_FLAG_CONCURRENT,
		   .invoke_command_entry_point = phase_a_invoke);
