/* SPDX-License-Identifier: BSD-2-Clause */
/* latstat.h - sample store and nearest-rank percentiles for the Mode P timing measurements (header only, host-testable).
 * Nearest-rank: p50 of n sorted samples is the sample at rank ceil(0.50*n); p99 is rank ceil(0.99*n); max is rank n. */
#ifndef LATSTAT_H
#define LATSTAT_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct latstat { uint32_t *v; unsigned n, cap, lost; };

static inline int latstat_init(struct latstat *s, unsigned cap) { s->v = (uint32_t *)malloc((size_t)cap * sizeof(uint32_t)); s->n = 0; s->cap = cap; s->lost = 0; return s->v ? 0 : -1; }
static inline void latstat_add(struct latstat *s, uint32_t x) { if (s->n < s->cap) s->v[s->n++] = x; else s->lost++; }
static inline int latstat_cmp(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }
static inline void latstat_sort(struct latstat *s) { if (s->n) qsort(s->v, s->n, sizeof(uint32_t), latstat_cmp); }
/* pct in 1..100, call after latstat_sort; returns 0 when empty */
static inline uint32_t latstat_pct(const struct latstat *s, unsigned pct)
{
	unsigned rank;
	if (!s->n) return 0;
	rank = (unsigned)(((uint64_t)pct * s->n + 99u) / 100u);	/* ceil(pct*n/100) */
	if (rank < 1) rank = 1;
	if (rank > s->n) rank = s->n;
	return s->v[rank - 1];
}
/* Histogram percentile: smallest bucket index whose cumulative count reaches ceil(pct*total/100); returns nb when the histogram is empty. */
static inline unsigned latstat_hist_pct(const uint32_t *bk, unsigned nb, unsigned pct)
{
	uint64_t total = 0, need, cum = 0; unsigned i;
	for (i = 0; i < nb; i++) total += bk[i];
	if (!total) return nb;
	need = (pct * total + 99u) / 100u;
	if (need < 1) need = 1;
	for (i = 0; i < nb; i++) { cum += bk[i]; if (cum >= need) return i; }
	return nb - 1;
}
#endif
