/* Host implementation of the mock platform declared in mock/rpi3_can_monitor_plat.h (shared by the harness). */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include "rpi3_can_monitor_plat.h"

__thread struct mock M;
int mock_profile_debug;
TEE_Result mock_init_extra_result;
unsigned mock_emsg_count;
int mock_fail_mac_alloc, mock_fail_mac_init, mock_fail_mac_final, mock_fail_rng;
unsigned mock_hmac_calls;
void (*mock_mac_final_hook)(void);

void mock_trace(char c) { if (M.ntrace < (int)sizeof(M.trace) - 1) M.trace[M.ntrace++] = c; }
uint64_t mock_timer(void)
{
	M.timer_reads++;
	if (M.tmode == 3) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
		return ((uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec) * 19200000ull / 1000000000ull; }
	if (M.tmode == 0) M.now += M.step; else if (M.tmode == 2) M.now -= M.step;
	if (M.held) M.timer_in_hold = M.now;
	return M.now;
}
bool mock_trylock(unsigned int *l)
{
	unsigned exp = 0;
	if (!(M.daif & MON_DAIF_IRQ)) M.violations++;		/* cpu_spin_trylock asserts IRQ masked */
	M.trylock_calls++;
	if (M.ext_hold) { if (M.ext_release_after && --M.ext_release_after == 0) M.ext_hold = 0; else return false; }
	if (!__atomic_compare_exchange_n(l, &exp, 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return false;
	M.locked_count++; M.held = true;
	if (M.hook_after_lock) M.hook_after_lock(M.hook_arg);
	return true;
}
uint32_t mock_mask_all(void) { uint32_t e = M.daif; M.daif = MON_DAIF_ALL; M.mask_calls++; return e; }
void mock_unmask(uint32_t e)
{
	if (!(e & MON_DAIF_IRQ) && M.locked_count) M.violations++;	/* IRQ must not be unmasked while a spinlock is held */
	M.daif = e; M.unmask_calls++; mock_trace('M');
}
void mock_unlock(unsigned int *l)
{
	if (!M.held) M.violations++;
	if (M.hook_before_unlock) M.hook_before_unlock(M.hook_arg);
	__atomic_store_n(l, 0u, __ATOMIC_RELEASE); M.locked_count--; M.held = false; mock_trace('U');
}
void mock_unlock_restore(unsigned int *l, uint32_t e) { mock_unlock(l); mock_unmask(e); }
void mock_inc32(volatile uint32_t *p) { (void)__atomic_fetch_add(p, 1u, __ATOMIC_RELAXED); }
void mock_emsg(const char *fmt, ...) { va_list a; va_start(a, fmt); va_end(a); mock_emsg_count++; }
void mock_lock_held_check(void) { if (!M.held) M.violations++; }
bool mock_exc_masked(void) { return (M.daif & (MON_DAIF_IRQ | MON_DAIF_FIQ)) == (MON_DAIF_IRQ | MON_DAIF_FIQ); }
uint8_t mock_tmp_stack[4096] __attribute__((aligned(16)));
void mock_panic(const char *msg) { (void)msg; M.panics++; }
TEE_Result mock_rng(void *buf, size_t len)
{
	size_t i; if (mock_fail_rng) return TEE_ERROR_GENERIC;
	for (i = 0; i < len; i++) ((uint8_t *)buf)[i] = (i & 1) ? 0xA5 : 0x5A;
	return TEE_SUCCESS;
}

/* ---- SHA-256 and HMAC-SHA256 (host only, for the mock crypto layer) ---- */
typedef struct { uint32_t h[8]; uint8_t b[64]; uint64_t n; size_t bl; } sha_t;
static const uint32_t K256[64] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_block(sha_t *s, const uint8_t *p)
{
	uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2; int i;
	for (i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
	for (i = 16; i < 64; i++) { uint32_t s0 = ROR(w[i-15],7) ^ ROR(w[i-15],18) ^ (w[i-15] >> 3), s1 = ROR(w[i-2],17) ^ ROR(w[i-2],19) ^ (w[i-2] >> 10); w[i] = w[i-16] + s0 + w[i-7] + s1; }
	a=s->h[0]; b=s->h[1]; c=s->h[2]; d=s->h[3]; e=s->h[4]; f=s->h[5]; g=s->h[6]; h=s->h[7];
	for (i = 0; i < 64; i++) { t1 = h + (ROR(e,6) ^ ROR(e,11) ^ ROR(e,25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i]; t2 = (ROR(a,2) ^ ROR(a,13) ^ ROR(a,22)) + ((a & b) ^ (a & c) ^ (b & c));
		h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2; }
	s->h[0]+=a; s->h[1]+=b; s->h[2]+=c; s->h[3]+=d; s->h[4]+=e; s->h[5]+=f; s->h[6]+=g; s->h[7]+=h;
}
static void sha_init(sha_t *s) { static const uint32_t i0[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}; memcpy(s->h, i0, 32); s->n = 0; s->bl = 0; }
static void sha_upd(sha_t *s, const uint8_t *d, size_t l) { while (l--) { s->b[s->bl++] = *d++; s->n++; if (s->bl == 64) { sha_block(s, s->b); s->bl = 0; } } }
static void sha_fin(sha_t *s, uint8_t *o)
{
	uint64_t bits = s->n * 8; int i; uint8_t pad = 0x80; sha_upd(s, &pad, 1); pad = 0; while (s->bl != 56) sha_upd(s, &pad, 1);
	for (i = 7; i >= 0; i--) { uint8_t x = (uint8_t)(bits >> (8*i)); sha_upd(s, &x, 1); }
	for (i = 0; i < 8; i++) { o[4*i] = s->h[i] >> 24; o[4*i+1] = s->h[i] >> 16; o[4*i+2] = s->h[i] >> 8; o[4*i+3] = s->h[i]; }
}
void host_hmac_sha256(const uint8_t *k, size_t kl, const uint8_t *m, size_t ml, uint8_t *out)
{
	uint8_t kb[64] = {0}, ip[64], op[64], in[32]; sha_t s; int i;
	if (kl > 64) { sha_init(&s); sha_upd(&s, k, kl); sha_fin(&s, kb); } else memcpy(kb, k, kl);
	for (i = 0; i < 64; i++) { ip[i] = kb[i] ^ 0x36; op[i] = kb[i] ^ 0x5c; }
	sha_init(&s); sha_upd(&s, ip, 64); sha_upd(&s, m, ml); sha_fin(&s, in);
	sha_init(&s); sha_upd(&s, op, 64); sha_upd(&s, in, 32); sha_fin(&s, out);
}
struct mctx { uint8_t key[64]; size_t kl; uint8_t msg[512]; size_t n; };
static struct mctx g_ctx;
TEE_Result mock_mac_alloc(void **ctx) { if (mock_fail_mac_alloc) return TEE_ERROR_OUT_OF_MEMORY; *ctx = &g_ctx; return TEE_SUCCESS; }
TEE_Result mock_mac_init(void *ctx, const uint8_t *k, size_t kl)
{ struct mctx *c = ctx; if (mock_fail_mac_init) return TEE_ERROR_GENERIC; if (!c || kl > 64) return TEE_ERROR_GENERIC; memcpy(c->key, k, kl); c->kl = kl; c->n = 0; return TEE_SUCCESS; }
TEE_Result mock_mac_update(void *ctx, const uint8_t *d, size_t l)
{ struct mctx *c = ctx; if (!c || c->n + l > sizeof c->msg) return TEE_ERROR_GENERIC; memcpy(c->msg + c->n, d, l); c->n += l; return TEE_SUCCESS; }
TEE_Result mock_mac_final(void *ctx, uint8_t *o, size_t ol)
{
	struct mctx *c = ctx; if (mock_fail_mac_final || !c || ol != 32) return TEE_ERROR_GENERIC;
	if (M.held) { M.violations++; M.hmac_in_hold++; }			/* the HMAC must run with the lock released */
	mock_hmac_calls++; host_hmac_sha256(c->key, c->kl, c->msg, c->n, o);
	if (mock_mac_final_hook) mock_mac_final_hook();
	return TEE_SUCCESS;
}

#ifdef CFG_RPI3_CAN_MONITOR_UART
struct mock_uart MU;
bool mock_uart_ready(void) { MU.lsr_reads++; return MU.mode == 0 || (MU.mode == 2 && MU.room > 0); }
void mock_uart_put(unsigned c) { if (MU.puts < sizeof MU.out) MU.out[MU.puts] = (unsigned char)c; MU.puts++; if (MU.mode == 2 && MU.room) MU.room--; }
#endif
