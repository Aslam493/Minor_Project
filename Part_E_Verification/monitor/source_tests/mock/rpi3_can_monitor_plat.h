/* Host mock of the platform bindings. Same name as the OP-TEE header so the REAL rpi3_can_monitor.c compiles
 * unchanged on the host. The mock records violations of the approved rules (IRQ-masked trylock, unlock-before-unmask,
 * lock-protected state touched without the lock). */
#ifndef __RPI3_CAN_MONITOR_PLAT_H
#define __RPI3_CAN_MONITOR_PLAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t TEE_Result;
#define TEE_SUCCESS		0x00000000u
#define TEE_ERROR_GENERIC	0xFFFF0000u
#define TEE_ERROR_BAD_PARAMETERS	0xFFFF0006u
#define TEE_ERROR_BAD_STATE	0xFFFF0007u
#define TEE_ERROR_NOT_SUPPORTED	0xFFFF000Au
#define TEE_ERROR_BUSY		0xFFFF000Du
#define TEE_ERROR_SHORT_BUFFER	0xFFFF0010u
#define TEE_ERROR_TIMEOUT	0xFFFF3001u
#define TEE_ERROR_OUT_OF_MEMORY	0xFFFF000Cu
#define TEE_NUM_PARAMS		4
#define TEE_PARAM_TYPE_NONE		0u
#define TEE_PARAM_TYPE_VALUE_INPUT	1u
#define TEE_PARAM_TYPE_VALUE_OUTPUT	2u
#define TEE_PARAM_TYPE_VALUE_INOUT	3u
#define TEE_PARAM_TYPE_MEMREF_INPUT	5u
#define TEE_PARAM_TYPE_MEMREF_OUTPUT	6u
#define TEE_PARAM_TYPE_MEMREF_INOUT	7u
#define TEE_PARAM_TYPES(t0, t1, t2, t3) ((t0) | ((t1) << 4) | ((t2) << 8) | ((t3) << 12))
typedef union { struct { void *buffer; size_t size; } memref; struct { uint32_t a; uint32_t b; } value; } TEE_Param;
#define SPINLOCK_UNLOCK		0
#define __nostackcheck
#define __aligned(x)		__attribute__((aligned(x)))
#define MON_DAIF_IRQ	0x1u	/* foreign interrupt in this build */
#define MON_DAIF_FIQ	0x2u	/* native interrupt in this build */
#define MON_DAIF_ALL	0x7u
struct mock {				/* thread-local: each thread plays one execution context */
	uint64_t now, step; int tmode;			/* 0 advance by step, 1 frozen, 2 backward, 3 real clock */
	uint32_t daif; unsigned locked_count; bool held;
	unsigned ext_hold, ext_release_after;		/* the lock is held by another context */
	unsigned trylock_calls, timer_reads, mask_calls, unmask_calls, violations, panics;
	void (*hook_after_lock)(void *); void (*hook_before_unlock)(void *); void *hook_arg;
	unsigned hmac_in_hold;
	uint64_t timer_in_hold;
	char trace[256]; int ntrace;
};
extern __thread struct mock M;
extern int mock_profile_debug;
extern TEE_Result mock_init_extra_result;
extern int mock_fail_mac_alloc, mock_fail_mac_init, mock_fail_mac_final, mock_fail_rng;
extern unsigned mock_hmac_calls;
extern void (*mock_mac_final_hook)(void);
void host_hmac_sha256(const uint8_t *k, size_t kl, const uint8_t *m, size_t ml, uint8_t *out);
extern unsigned mock_emsg_count;
uint64_t mock_timer(void);
bool mock_trylock(unsigned int *l);
void mock_unlock_restore(unsigned int *l, uint32_t e);
uint32_t mock_mask_all(void);
void mock_unmask(uint32_t e);
void mock_inc32(volatile uint32_t *p);
void mock_emsg(const char *fmt, ...);
void mock_lock_held_check(void);
TEE_Result mock_init_extra(void);
void mock_trace(char c);
void mock_unlock(unsigned int *l);
bool mock_exc_masked(void);
void mock_panic(const char *msg);
TEE_Result mock_mac_alloc(void **ctx);
TEE_Result mock_mac_init(void *ctx, const uint8_t *k, size_t kl);
TEE_Result mock_mac_update(void *ctx, const uint8_t *d, size_t l);
TEE_Result mock_mac_final(void *ctx, uint8_t *o, size_t ol);
TEE_Result mock_rng(void *buf, size_t len);
#define MON_PLAT_MASK_ALL()		mock_mask_all()
#define MON_PLAT_UNMASK(e)		mock_unmask(e)
#define MON_PLAT_TIMER()		mock_timer()
/* stack watermark mock: a 4096-byte array stands for the tmp stack; the "stack pointer" is 1024 bytes below its top */
extern uint8_t mock_tmp_stack[4096];
#define MON_PLAT_TMP_STACK_TOP()	((uint64_t)(uintptr_t)(mock_tmp_stack + 4096))
#define MON_PLAT_TMP_STACK_SIZE()	4096ULL
#define MON_PLAT_SP_BELOW()		((uint64_t)(uintptr_t)(mock_tmp_stack + 4096 - 1024))
/* W1 mock: a fixed translation result (valid, attr 0xFF, inner shareable, PA 0x12345000) and fixed SCTLR/MAIR */
#define MON_PLAT_W1_PROBE(va, par, sctlr, mair) do { (void)(va); (par) = 0xFF00000012345000ULL | (3ULL << 7); (sctlr) = 0x30D0199DULL; (mair) = 0xFFULL | (0x04ULL << 8); } while (0)
#define MON_PLAT_TRYLOCK(l)		mock_trylock(l)
#define MON_PLAT_UNLOCK_RESTORE(l, e)	mock_unlock_restore((l), (e))
#define MON_PLAT_INC32(p)		mock_inc32(p)
#define MON_PLAT_LOAD32(p)		__atomic_load_n((p), __ATOMIC_RELAXED)
#define MON_PLAT_STORE32(p, v)		__atomic_store_n((p), (v), __ATOMIC_RELAXED)
#define MON_PLAT_LOAD64(p)		__atomic_load_n((p), __ATOMIC_RELAXED)
#define MON_PLAT_STORE64(p, v)		__atomic_store_n((p), (v), __ATOMIC_RELAXED)
#define MON_PLAT_LOAD_ACQUIRE32(p)	__atomic_load_n((p), __ATOMIC_ACQUIRE)
#define MON_PLAT_STORE_RELEASE32(p, v)	__atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define MON_PLAT_UNLOCK(l)		mock_unlock(l)
#define MON_PLAT_EXC_FIQ_IRQ_MASKED()	mock_exc_masked()
#define MON_PLAT_PANIC(msg)		mock_panic(msg)
#define MON_PLAT_MAC_ALLOC(ctxp)	mock_mac_alloc(ctxp)
#define MON_PLAT_MAC_INIT(ctx, k, kl)	mock_mac_init((ctx), (k), (kl))
#define MON_PLAT_MAC_UPDATE(ctx, d, l)	mock_mac_update((ctx), (d), (l))
#define MON_PLAT_MAC_FINAL(ctx, o, ol)	mock_mac_final((ctx), (o), (ol))
#define MON_PLAT_RNG(buf, len)		mock_rng((buf), (len))
#define MON_PLAT_EMSG(...)		mock_emsg(__VA_ARGS__)
#define MON_PLAT_REGISTER_INIT(fn)	_Static_assert(1, "the harness calls the init function itself")
#define MON_PLAT_LOCK_HELD_CHECK()	mock_lock_held_check()
#define MON_PLAT_INIT_EXTRA()		mock_init_extra()
#define MON_PLAT_PROFILE_DEBUG		(mock_profile_debug)

#ifdef CFG_RPI3_CAN_MONITOR_UART
/* Mock mini UART: map_result 0 = no mapping (feature off). mode 0 = always ready, 1 = never ready, 2 = ready while 'room' > 0 (put uses one). */
struct mock_uart { uintptr_t map_result; int mode; unsigned room; unsigned lsr_reads; unsigned puts; unsigned char out[4096]; };
extern struct mock_uart MU;
bool mock_uart_ready(void);
void mock_uart_put(unsigned c);
#define MON_PLAT_UART_MAP()		(MU.map_result)
#define MON_PLAT_UART_TX_READY(b)	mock_uart_ready()
#define MON_PLAT_UART_PUT(b, c)		mock_uart_put(c)
#endif
#endif
