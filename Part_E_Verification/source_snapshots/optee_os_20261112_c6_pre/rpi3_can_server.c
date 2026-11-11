// SPDX-License-Identifier: BSD-2-Clause
/*
 * Plan A, Steps 4/5: classify/authenticate logic and the cross-core
 * result-push protocol for core 1's permanent Secure-World loop (see
 * KNOWLEDGE.md and /home/aslam/.claude/plans/first-you-do-one-swirling-sunrise.md).
 *
 * This logic is a direct port of secure_can_demo/ta/secure_can_ta.c's
 * compute_frame_tag()/classify_frame()/verify_frame() - same algorithm
 * (HMAC-SHA256 over can_id+data, ID-threshold classification, constant-time
 * tag comparison) and the same demo key, reused verbatim. The port is
 * necessary, not cosmetic: secure_can_ta.c is a user-mode Trusted
 * Application (S-EL0), using the sandboxed TEE_* Internal API
 * (TEE_AllocateTransientObject/TEE_MACInit/TEE_MACComputeFinal). Core 1's
 * permanent loop runs as OP-TEE core code (S-EL1) and cannot call into a
 * loaded TA's sandboxed API - it uses OP-TEE core's own internal crypto API
 * instead (crypto_mac_*, core/include/crypto/crypto.h), the same API
 * core/tee/tee_rpmb_fs.c already uses for its own HMAC-SHA256 (see
 * tee_rpmb_fs.c's rpmb_calc_hmac_hash(), lines ~380-420, used as the
 * reference calling pattern here).
 *
 * WIRED IN: added to plat-rpi3/sub.mk. rpi3_can_server_loop() below is
 * currently only its Step-3-sub-step-1 trivial body (a free-running
 * heartbeat) - it does not yet call the classify/authenticate functions
 * above or push a result; those are guarded behind
 * RPI3_CAN_SERVER_CLASSIFY_ENABLED (currently undefined) until Step 5,
 * deliberately deferred so the "does core 1 survive being permanently
 * redirected here" question gets isolated and verified before the security
 * logic and FIQ trigger point are wired in on top of it.
 *
 * rpi3_can_server_wake_core1() is the Normal-World-triggered wake path: the
 * design requires core 1 to be woken only *after* boot fully completes, not
 * during the boot chain itself - so unlike the rest of this project's
 * warm-boot mechanisms, this one is initiated on demand, from a debug PTA
 * command, once Normal World userspace signals it's ready. See that
 * function's own comment for the full call chain.
 */
#include <arm.h>
#include <compiler.h>
#include <crypto/crypto.h>
#include <inttypes.h>
#include <io.h>
#include <kernel/thread.h>
#include <kernel/thread_arch.h>
#include <mm/core_memprot.h>
#include <sm/optee_smc.h>
#include <sm/teesmc_opteed.h>
#include <sm/teesmc_opteed_macros.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <tee_api_defines.h>
#include <trace.h>

#include "platform_config.h"
#ifdef CFG_RPI3_CAN_MONITOR
#include <rpi3_can_monitor.h>
#endif

struct rpi3_can_server_result;

/* Exported to whatever calls into this file once Step 5 wires the real
 * classify/authenticate/push logic into rpi3_can_server_loop() below.
 */
void rpi3_can_server_push_result(struct rpi3_can_server_result *dst,
				 uint32_t seq, uint32_t can_id,
				 const uint8_t data[8], uint32_t classification,
				 const uint8_t auth_tag[8]);
void rpi3_can_server_unmask_own_mailbox(uint32_t core_pos);

/* Step 4.4D: defined in rpi3_can_fiq.c. Non-blocking - returns immediately
 * whether or not a frame was actually pending. This is now the ONLY place
 * CAN ID/counter/timestamp/HMAC validation runs; it used to run inside the
 * FIQ (interrupt_main_handler(), same file) and a real hardware test showed
 * that hung the whole board - see rpi3_can_fiq.c's top-of-file comment and
 * can0_hmac_ctx's own comment for the full account of what changed and why.
 */
bool rpi3_can_fiq_process_pending_frame(void);

/* Exported to the new TF-A/OP-TEE "secure server" vector (Step 3) and to a
 * debug PTA command for reading the heartbeat from Normal World.
 */
uint64_t rpi3_can_server_get_heartbeat(void);
void __noreturn rpi3_can_server_loop(void);

/* Exported to a debug PTA command, invoked from Normal World once boot is
 * complete, to trigger core 1's permanent wake-into-Secure-World.
 */
int32_t rpi3_can_server_wake_core1(void);

/* Duplicated one-line MMIO-mapping helper rather than exposing
 * rpi3_can_fiq.c's own static arm_local_base() - deliberately avoids
 * touching that already-proven-working file for a change this unrelated to
 * its own job (FIQ handling). ARM_LOCAL_BASE/ARM_LOCAL_REG_SIZE come from
 * platform_config.h, same as rpi3_can_fiq.c uses.
 */
static vaddr_t rpi3_can_server_arm_local_base(void)
{
	return core_mmu_get_va(ARM_LOCAL_BASE, MEM_AREA_IO_SEC,
			       ARM_LOCAL_REG_SIZE);
}

/* Mailbox 1's per-core SET/CLR registers - mailbox 0 is Linux's own SMP IPI
 * channel (linux/drivers/irqchip/irq-bcm2836.c), left untouched. Mailboxes
 * 1-3 are unused by this Linux build; mailbox 1 is used here for core 1's
 * one-directional result push to core 0 (see the plan file's Step 4 for why
 * this is one-directional, not request/response). Offsets confirmed against
 * linux/include/linux/irqchip/irq-bcm2836.h (LOCAL_MAILBOX0_SET0=0x080,
 * LOCAL_MAILBOX3_SET0=0x08c -> 4 bytes/mailbox; LOCAL_MAILBOX0_CLR0=0x0c0,
 * LOCAL_MAILBOX3_CLR0=0x0cc), not hand-derived.
 */
#define ARM_LOCAL_MAILBOX1_SET0_OFFSET      0x084U
#define ARM_LOCAL_MAILBOX1_CLR0_OFFSET      0x0c4U
#define ARM_LOCAL_MAILBOX_CORE_STRIDE       0x10U /* 16 bytes/core, both banks */
#define LOCAL_MAILBOX_INT_CONTROL0_OFFSET   0x050U
#define RPI3_CAN_SERVER_CORE0_POS           0U
#define RPI3_CAN_SERVER_CORE1_POS           1U
#define RPI3_CAN_SERVER_DOORBELL_BIT        BIT32(0)

/* --- Mirrors secure_can_demo/ta/include/secure_can_ta.h exactly - see that
 * file for the canonical definitions this must stay in sync with. ---
 */
#define SECURE_CAN_FRAME_DATA_LEN  8U
#define SECURE_CAN_FRAME_TAG_LEN   8U
#define SECURE_CAN_CRITICAL_ID_MAX 0x102U
#define SECURE_CAN_CLASS_NORMAL    0U
#define SECURE_CAN_CLASS_SECURE    1U

/*
 * Step 4/5 (classify/authenticate) - deliberately not compiled in until
 * Test A (Step 3's trivial-heartbeat-only body) has been verified on real
 * hardware, per the plan's own staged approach. Guarded (rather than left
 * unguarded and unused) because these are all `static` functions with no
 * caller yet - `-Werror -Wunused-function`/`-Wunused-const-variable`
 * (optee_os/mk/compile.mk:25) would otherwise fail the build the moment
 * this file is added to plat-rpi3/sub.mk.
 */
#ifdef RPI3_CAN_SERVER_CLASSIFY_ENABLED
struct rpi3_can_server_frame {
	uint32_t can_id;
	uint8_t data[SECURE_CAN_FRAME_DATA_LEN];
};

#define RPI3_CAN_SERVER_KEY_LEN 32U

/* Identical to secure_can_demo_key in secure_can_ta.c - same prototype-only
 * caveat applies (compiled into the binary, not provisioned via secure
 * storage; see that file's own comment).
 */
static const uint8_t rpi3_can_server_key[RPI3_CAN_SERVER_KEY_LEN] = {
	0x4b, 0x1e, 0x9a, 0x77, 0x3c, 0xd2, 0x08, 0xf5,
	0x61, 0xae, 0x2f, 0x90, 0xc3, 0x14, 0x7b, 0xe6,
	0x82, 0x35, 0xda, 0x0c, 0x59, 0xf1, 0xa4, 0x6d,
	0x27, 0xbb, 0x93, 0x40, 0xe8, 0x1c, 0x66, 0xf9,
};

/* Port of secure_can_ta.c's compute_frame_tag(), using OP-TEE core's
 * internal crypto_mac_* API (core/include/crypto/crypto.h) in place of the
 * TA-sandboxed TEE_AllocateTransientObject/TEE_MACInit/TEE_MACComputeFinal
 * calls - same algorithm (HMAC-SHA256 over can_id+data), same key, same
 * message layout. Calling pattern matches tee_rpmb_fs.c's
 * rpmb_calc_hmac_hash() (alloc ctx -> init with raw key bytes -> update ->
 * final -> free ctx), the only other core-level HMAC-SHA256 user in this
 * tree.
 */
static TEE_Result rpi3_can_server_compute_frame_tag(
		const struct rpi3_can_server_frame *frame,
		uint8_t *tag, size_t tag_len)
{
	void *ctx = NULL;
	TEE_Result res;
	uint8_t msg[sizeof(frame->can_id) + SECURE_CAN_FRAME_DATA_LEN];
	uint8_t full_tag[32]; /* HMAC-SHA256 output size */

	/* Defensive: today's only caller always passes tag_len=8, but this
	 * function has no other guard against a future caller passing more
	 * than full_tag actually holds - see review note in the plan file.
	 */
	if (tag_len > sizeof(full_tag))
		return TEE_ERROR_BAD_PARAMETERS;

	memcpy(msg, &frame->can_id, sizeof(frame->can_id));
	memcpy(msg + sizeof(frame->can_id), frame->data,
	       SECURE_CAN_FRAME_DATA_LEN);

	res = crypto_mac_alloc_ctx(&ctx, TEE_ALG_HMAC_SHA256);
	if (res)
		return res;

	res = crypto_mac_init(ctx, rpi3_can_server_key,
			      RPI3_CAN_SERVER_KEY_LEN);
	if (res)
		goto out;

	res = crypto_mac_update(ctx, msg, sizeof(msg));
	if (res)
		goto out;

	res = crypto_mac_final(ctx, full_tag, sizeof(full_tag));
	if (res)
		goto out;

	memcpy(tag, full_tag, tag_len);

out:
	crypto_mac_free_ctx(ctx);
	return res;
}

/* Port of secure_can_ta.c's classify_frame() - identical ID-threshold rule. */
static uint32_t rpi3_can_server_classify_frame(
		const struct rpi3_can_server_frame *frame)
{
	uint32_t class = (frame->can_id <= SECURE_CAN_CRITICAL_ID_MAX) ?
			 SECURE_CAN_CLASS_SECURE : SECURE_CAN_CLASS_NORMAL;

	IMSG("rpi3_can_server: classified CAN frame id=0x%03" PRIx32 " as %s",
	     frame->can_id, class == SECURE_CAN_CLASS_SECURE ?
	     "SECURE (authenticate)" : "NORMAL (push to core 0 directly)");
	return class;
}

/* Port of secure_can_ta.c's tags_match() - identical constant-time compare,
 * same rationale (no timing side channel for a Normal World attacker to
 * exploit one byte at a time).
 */
static bool rpi3_can_server_tags_match(const uint8_t *a, const uint8_t *b,
				       size_t len)
{
	uint8_t diff = 0;
	size_t i;

	for (i = 0; i < len; i++)
		diff |= a[i] ^ b[i];

	return diff == 0;
}

/* Port of secure_can_ta.c's verify_frame() - recomputes the tag and compares
 * constant-time. Used by Step 3's loop for the SECURE path: classify, then
 * authenticate (compute the tag to push to core 0), with verify available
 * for the same re-verification use case secure_can_demo already exercises.
 */
static bool rpi3_can_server_verify_frame(
		const struct rpi3_can_server_frame *frame,
		const uint8_t *claimed_tag)
{
	uint8_t full_tag[SECURE_CAN_FRAME_TAG_LEN];
	TEE_Result res;

	res = rpi3_can_server_compute_frame_tag(frame, full_tag,
						sizeof(full_tag));
	if (res != TEE_SUCCESS) {
		EMSG("rpi3_can_server: frame verification failed: 0x%08" PRIx32,
		     res);
		return false;
	}

	return rpi3_can_server_tags_match(claimed_tag, full_tag,
					  SECURE_CAN_FRAME_TAG_LEN);
}
#endif /* RPI3_CAN_SERVER_CLASSIFY_ENABLED */

/* --- Step 4: cross-core result-push protocol ---
 *
 * One struct, written only by core 1, read only by core 0 - see the plan's
 * "Flow direction" note: core 0 never asks core 1 for anything, it only
 * ever reacts to what core 1 already decided. `seq` doubles as the
 * validity flag: core 0 only trusts a result once it observes a `seq` it
 * hasn't already consumed.
 *
 * Deliberately NOT placed at a hardcoded physical address here - picking an
 * arbitrary NS DRAM address without a matching Linux `reserved-memory` DT
 * node risks silent memory corruption if it collides with something Linux's
 * own allocator later hands out. The actual buffer address is a separate,
 * still-open follow-up task (add a `reserved-memory` node, pass that
 * address in here) - rpi3_can_server_push_result() takes the destination
 * pointer as a parameter so this protocol logic can be written, reviewed,
 * and unit-reasoned-about now without guessing at that address.
 */
struct rpi3_can_server_result {
	uint32_t seq;
	uint32_t can_id;
	uint8_t data[SECURE_CAN_FRAME_DATA_LEN];
	uint32_t classification;
	uint8_t auth_tag[SECURE_CAN_FRAME_TAG_LEN];
	uint32_t status; /* 1 = valid result, 0 = reserved/unused for now */
};

#define RPI3_CAN_SERVER_STATUS_VALID 1U

/* Called from core 1 only. Writes the result into the (caller-supplied,
 * already NS-DRAM-reserved) shared struct, then rings core 0's mailbox 1
 * doorbell. The dsb() before the doorbell write is required, not optional,
 * despite these cores being cache-coherent: coherency guarantees core 0
 * will eventually observe the same values, not that it observes them
 * *before* the doorbell fires - without the barrier, core 0 could see the
 * doorbell and read a partially-written struct. This is the exact ordering
 * mistake the plan calls out explicitly as classic and intermittent.
 */
void rpi3_can_server_push_result(struct rpi3_can_server_result *dst,
				 uint32_t seq, uint32_t can_id,
				 const uint8_t data[SECURE_CAN_FRAME_DATA_LEN],
				 uint32_t classification,
				 const uint8_t auth_tag[SECURE_CAN_FRAME_TAG_LEN])
{
	vaddr_t local = rpi3_can_server_arm_local_base();

	dst->can_id = can_id;
	memcpy(dst->data, data, SECURE_CAN_FRAME_DATA_LEN);
	dst->classification = classification;
	if (auth_tag)
		memcpy(dst->auth_tag, auth_tag, SECURE_CAN_FRAME_TAG_LEN);
	else
		memset(dst->auth_tag, 0, SECURE_CAN_FRAME_TAG_LEN);
	dst->status = RPI3_CAN_SERVER_STATUS_VALID;
	/* seq written last, after everything it validates */
	dst->seq = seq;

	dsb();

	io_write32(local + ARM_LOCAL_MAILBOX1_SET0_OFFSET +
		   ARM_LOCAL_MAILBOX_CORE_STRIDE * RPI3_CAN_SERVER_CORE0_POS,
		   RPI3_CAN_SERVER_DOORBELL_BIT);
}

/* Called from core 1's own init (once, before its first push) to unmask its
 * chosen mailbox's interrupt line on itself - included here for
 * completeness of the protocol even though core 1's own loop design (plan
 * Step 3) polls rather than takes an actual IRQ for this; core 0's own
 * unmask (if it chooses to take a real interrupt rather than poll) is
 * Normal-World/Linux-side work, not this file's concern.
 */
void rpi3_can_server_unmask_own_mailbox(uint32_t core_pos)
{
	vaddr_t local = rpi3_can_server_arm_local_base();
	uint32_t ctrl;

	ctrl = io_read32(local + LOCAL_MAILBOX_INT_CONTROL0_OFFSET +
			 4U * core_pos);
	/* bit N enables mailbox N's IRQ for this core - confirmed against
	 * Linux's own bcm2836_arm_irqchip_unmask_per_cpu_irq() call for
	 * mailbox 0 (irq-bcm2836.c: unmask_per_cpu_irq(..., bit=0, cpu)),
	 * not hand-derived.
	 */
	ctrl |= BIT32(1);
	io_write32(local + LOCAL_MAILBOX_INT_CONTROL0_OFFSET + 4U * core_pos,
		  ctrl);
}

/*
 * Plan A Step 3, sub-step 1 (trivial body): a free-running heartbeat,
 * observable from Normal World via a debug PTA command, so a stuck core 1
 * is visible rather than a silent black box. No mailbox/shared-memory logic
 * yet - deliberately isolates "does core 1 survive being redirected here
 * and run forever" from every later question, per the plan's own staged
 * verification approach.
 */
static volatile uint64_t rpi3_can_server_heartbeat;

uint64_t rpi3_can_server_get_heartbeat(void)
{
	return rpi3_can_server_heartbeat;
}

/*
 * Entered once, forever, from the new TF-A/OP-TEE "secure server" vector
 * (see the plan's Step 3 third-pass update) - core 1 never returns from
 * here and never reaches a Non-Secure eret again.
 *
 * CPU_ON leaves native (and foreign) interrupts masked - THREAD_EXCP_ALL is
 * set by init_secondary_helper() (core/arch/arm/kernel/boot.c) for every
 * secondary core, on the assumption that unmasking happens later, on the
 * way into Non-Secure. Core 1 never takes that path anymore, so nothing
 * else will ever unmask them - do it explicitly here, or the CAN FIQ
 * (Secure Peripheral Ownership, already proven working) will look dead on
 * this core even though nothing else is wrong.
 *
 * STEP 4.4D: this loop is now also where CAN frame validation (including
 * HMAC-SHA256) actually runs, via rpi3_can_fiq_process_pending_frame() -
 * see that function's own comment (rpi3_can_fiq.c) and this file's own
 * extern declaration of it above for why it moved here from the FIQ.
 * Polled every iteration (no sleep/wait), matching this loop's existing
 * "poll rather than take a real interrupt" design already noted on
 * rpi3_can_server_unmask_own_mailbox() above - cheap when idle (the
 * function returns immediately if nothing is pending) and never adds an
 * unbounded wait to this loop.
 *
 * IMPORTANT CAVEAT (verified during this change, not assumed): moving this
 * call here removes the *FIQ-nesting* problem that hung the board, but this
 * loop still executes on the same physical stack_tmp buffer the FIQ itself
 * uses (both SVC-mode and FIQ-mode SP are set to the same
 * tmp_stack_va_end by cpu_on_handler(), core/arch/arm/kernel/entry_a32.S) -
 * see can0_hmac_ctx's own comment in rpi3_can_fiq.c for the full account.
 * This is not a second, larger, dedicated stack.
 */
void __noreturn rpi3_can_server_loop(void)
{
#ifdef CFG_RPI3_CAN_MONITOR
	/*
	 * Secure CAN Monitor build, decision D1 = Option B (design 4g C7): FIQ and IRQ stay masked for the WHOLE loop.
	 * CPU_ON left every exception masked and nothing here unmasks any of them, so no FIQ can ever be taken on top
	 * of the live loop frames (the O-01 stack overlap cannot occur). The legacy chip A frame path, which reaches
	 * HMAC and the heap, is not run in this build (F27: Core 1 never calls HMAC or the shared heap).
	 */
	uint32_t mon_pass = 0;
	bool mon_on;

	rpi3_mon_core1_entry_check();		/* panics if FIQ or IRQ is unmasked */
	mon_on = rpi3_mon_init_ok();		/* a failed monitor init leaves Core 1 doing nothing but the heartbeat */
#else
	thread_set_exceptions(thread_get_exceptions() & ~THREAD_EXCP_NATIVE_INTR);
#endif

	while (true) {
		rpi3_can_server_heartbeat++;
#ifdef CFG_RPI3_CAN_MONITOR
		if (mon_on)
			rpi3_mon_core1_pass(mon_pass++);	/* G7 update inline; one lock attempt per 256 passes */
#else
		rpi3_can_fiq_process_pending_frame();
#endif
		dsb();
	}
}

/*
 * Called from a debug PTA command (Normal World userspace invokes this once
 * boot is fully complete - by design, core 1 must NOT be woken during the
 * boot chain itself, only afterward). Issues the project-specific
 * RPI3_OPTEED_WAKE_CORE1 SMC (sm/teesmc_opteed.h) via thread_smc() - the
 * same generic outbound-SMC trampoline already used elsewhere in OP-TEE
 * core for SMCCC_VERSION/SMCCC_ARCH_FEATURES probes (thread.c). This SMC is
 * handled by TF-A's opteed_smc_handler() (services/spd/opteed/opteed_main.c),
 * which calls psci_cpu_on(1, ...) directly as a plain EL3 C function call -
 * not a PSCI SMC re-issue, so PSCI's is_caller_secure() gate (which would
 * otherwise reject a Secure-World-originated PSCI request) never applies.
 *
 * Core 1 then wakes via the exact same, already-proven CPU_ON path used for
 * cores 2/3 today, and is intercepted by
 * opteed_enter_secure_server_if_core1() (opteed_pm.c) at
 * bl31_warm_entrypoint's tail - strictly after psci_warmboot_entrypoint()
 * has released its power-domain lock - before it would ever reach the
 * Non-Secure eret psci_cpu_on() would otherwise set up for it. From there
 * it enters rpi3_can_server_loop() above and never returns.
 *
 * Returns the raw PSCI status code (0 = PSCI_E_SUCCESS) so the caller can
 * detect and report a failure (e.g. PSCI_E_ALREADY_ON if called twice)
 * rather than silently assuming success.
 */
int32_t rpi3_can_server_wake_core1(void)
{
	return (int32_t)thread_smc(RPI3_OPTEED_WAKE_CORE1, 0, 0, 0);
}
