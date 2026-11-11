/* Host mock of the platform primitives used by the Component 2.2 synchronization logic.
 * In a future OP-TEE build the MON_* macros would map to the real primitives; here they are mocks
 * that record violations of the approved rules (lock-held access, IRQ-masked trylock, unlock-before-unmask). */
#ifndef MON_PLAT_H
#define MON_PLAT_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define MON_DAIF_IRQ 0x1u   /* the foreign interrupt in this build */
#define MON_DAIF_FIQ 0x2u   /* the native interrupt in this build */
#define MON_DAIF_ALL 0x7u
struct mock {
	uint64_t now, step; int tmode;          /* tmode 0 advance, 1 frozen, 2 backward */
	uint32_t daif; unsigned locked_count; bool held;
	unsigned ext_hold, ext_release_after;   /* lock held by another context */
	unsigned trylock_calls, timer_reads, violations, scan_visits;
	bool timer_read_in_hold; uint64_t timer_read_while_held;
	char trace[256]; int ntrace;
};
extern struct mock M;
uint64_t mock_timer(void);
bool mock_trylock(unsigned *l);
void mock_unlock(unsigned *l);
uint32_t mock_mask_all(void);
void mock_unmask(uint32_t e);
void mock_trace(char c);
#define MON_TIMER()          mock_timer()
#define MON_TRYLOCK(l)       mock_trylock(l)
#define MON_UNLOCK(l)        mock_unlock(l)
#define MON_MASK_ALL()       mock_mask_all()
#define MON_UNMASK(e)        mock_unmask(e)
#define MON_ATOMIC_INC32(p)  ((void)__atomic_fetch_add((p), 1u, __ATOMIC_RELAXED))
#define MON_LOAD32(p)        __atomic_load_n((p), __ATOMIC_RELAXED)
#define MON_STORE32(p, v)    __atomic_store_n((p), (v), __ATOMIC_RELAXED)
#define MON_LOAD64(p)        __atomic_load_n((p), __ATOMIC_RELAXED)
#define MON_STORE64(p, v)    __atomic_store_n((p), (v), __ATOMIC_RELAXED)
#endif
