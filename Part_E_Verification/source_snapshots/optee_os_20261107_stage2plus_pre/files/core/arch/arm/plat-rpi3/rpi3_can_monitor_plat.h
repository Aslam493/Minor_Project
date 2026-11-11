/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Platform bindings for rpi3_can_monitor.c (OP-TEE build).
 *
 * The monitor logic is written once and uses only the MON_PLAT_* names below. The host test harness
 * (Part_E_Verification/monitor/source_tests) provides a mock header with the SAME NAME, so the same
 * rpi3_can_monitor.c compiles on the host and in OP-TEE.
 *
 * Design: Part_E_Verification/monitor/MONITOR_STATE_DESIGN.md, sections 4a to 4k.
 */
#ifndef __RPI3_CAN_MONITOR_PLAT_H
#define __RPI3_CAN_MONITOR_PLAT_H

#ifndef ARM64
#error "rpi3_can_monitor requires an AArch64 core: lock-free G7 words rely on 64-bit single-copy atomic loads and stores"
#endif

#include <arm.h>
#include <atomic.h>
#include <compiler.h>
#include <initcall.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <tee_api_types.h>
#include <trace.h>

/* Exceptions: mask everything while trying for the lock, restore on release (design 4f B3). */
#define MON_PLAT_MASK_ALL()		thread_mask_exceptions(THREAD_EXCP_ALL)
#define MON_PLAT_UNMASK(e)		thread_unmask_exceptions(e)

/* The 64-bit secure counter, 19.2 MHz. barrier_read_counter_timer() is __noprof (no compiler hook). */
#define MON_PLAT_TIMER()		barrier_read_counter_timer()

/* The one monitor lock (OP-TEE spinlock). cpu_spin_trylock() asserts that IRQ is masked. */
#define MON_PLAT_TRYLOCK(l)		cpu_spin_trylock(l)
#define MON_PLAT_UNLOCK_RESTORE(l, e)	cpu_spin_unlock_xrestore((l), (e))

/* Atomics. atomic_inc32() is hand-written assembly (ldaxr/stxr loop), not instrumented. The relaxed
 * load/store forms are the same operation atomic_load_u32()/atomic_store_u32() perform, written as
 * builtins so that no compiler hook call is added (design 4a, rule 2). */
#define MON_PLAT_INC32(p)		((void)atomic_inc32(p))
#define MON_PLAT_LOAD32(p)		__atomic_load_n((p), __ATOMIC_RELAXED)
#define MON_PLAT_STORE32(p, v)		__atomic_store_n((p), (v), __ATOMIC_RELAXED)
#define MON_PLAT_LOAD64(p)		__atomic_load_n((p), __ATOMIC_RELAXED)
#define MON_PLAT_STORE64(p, v)		__atomic_store_n((p), (v), __ATOMIC_RELAXED)
#define MON_PLAT_LOAD_ACQUIRE32(p)	__atomic_load_n((p), __ATOMIC_ACQUIRE)
#define MON_PLAT_STORE_RELEASE32(p, v)	__atomic_store_n((p), (v), __ATOMIC_RELEASE)

#define MON_PLAT_EMSG(...)		EMSG(__VA_ARGS__)
#define MON_PLAT_REGISTER_INIT(fn)	driver_init(fn)

/* Host mock only: checks that lock-protected state is touched with the lock held. No cost here. */
#define MON_PLAT_LOCK_HELD_CHECK()	((void)0)

/* Extra initialisation steps run by rpi3_mon_init() before init_ok is published. Stage 1 has none; later
 * stages (mac contexts, boot_id) add their steps here or next to it, always BEFORE the publication. */
#define MON_PLAT_INIT_EXTRA()		TEE_SUCCESS

#ifdef CFG_RPI3_CAN_MONITOR_DEBUG
#define MON_PLAT_PROFILE_DEBUG		1
#else
#define MON_PLAT_PROFILE_DEBUG		0
#endif

#endif /* __RPI3_CAN_MONITOR_PLAT_H */
