/* host test for latstat.h */
#include <stdio.h>
#include "latstat.h"
static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("  FAIL %s\n", m); fails++; } else printf("  ok   %s\n", m); } while (0)
int main(void)
{
	struct latstat s; unsigned i;
	latstat_init(&s, 1000);
	CHECK(latstat_pct(&s, 50) == 0, "empty store gives 0");
	for (i = 100; i >= 1; i--) latstat_add(&s, i * 10);	/* 1000 down to 10, added in reverse */
	latstat_sort(&s);
	CHECK(latstat_pct(&s, 50) == 500, "p50 of 10..1000 step 10 is 500 (rank 50)");
	CHECK(latstat_pct(&s, 99) == 990, "p99 is 990 (rank 99)");
	CHECK(latstat_pct(&s, 100) == 1000, "max (p100) is 1000");
	CHECK(latstat_pct(&s, 1) == 10, "p1 is 10");
	free(s.v); latstat_init(&s, 3);
	latstat_add(&s, 5); latstat_add(&s, 1); latstat_add(&s, 9); latstat_add(&s, 7);
	CHECK(s.n == 3 && s.lost == 1, "capacity 3: the 4th sample is counted as lost, not stored");
	latstat_sort(&s);
	CHECK(latstat_pct(&s, 50) == 5 && latstat_pct(&s, 99) == 9 && latstat_pct(&s, 100) == 9, "p50/p99/max of {1,5,9}: 5, 9, 9");
	free(s.v); latstat_init(&s, 10); latstat_add(&s, 42); latstat_sort(&s);
	CHECK(latstat_pct(&s, 50) == 42 && latstat_pct(&s, 99) == 42, "single sample: every percentile is that sample");
	free(s.v);
	{ uint32_t bk[24] = {0}; bk[1] = 90; bk[3] = 9; bk[23] = 1;		/* 100 answers */
	  CHECK(latstat_hist_pct(bk, 24, 50) == 1, "histogram p50 is bucket 1");
	  CHECK(latstat_hist_pct(bk, 24, 90) == 1, "histogram p90 (rank 90) is still bucket 1");
	  CHECK(latstat_hist_pct(bk, 24, 99) == 3, "histogram p99 (rank 99) is bucket 3");
	  CHECK(latstat_hist_pct(bk, 24, 100) == 23, "histogram p100 is the last bucket");
	  memset(bk, 0, sizeof bk); CHECK(latstat_hist_pct(bk, 24, 50) == 24, "empty histogram returns nb"); }
	printf("latstat_test: %s\n", fails ? "FAILED" : "all passed"); return fails != 0;
}
