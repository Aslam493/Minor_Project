#!/usr/bin/env python3
"""Soak analysis: read the CSV written by soak_watch.sh and judge it against the pass criteria fixed in advance
(RESULTS.md section 6). Read-only. usage: analyze_soak.py <soak_watch.csv> [--poll-limit-us N] [--handoff <UTC prefix>]...
  --handoff 2026-11-12T13:4   marks the minute(s) where soak 1 ended and soak 2 started (the chain pauses the sender for
                              about 2 s plus start-up). Alarm rises in samples whose time starts with that prefix are
                              printed as HANDOFF (a real pause in the test sender, correctly detected) and are not counted
                              as false positives. Use the end time in /tmp/soak_1.end on the Pi to choose the prefix.

Criteria (each prints PASS / FAIL / REVIEW):
  C1 no sample lost          : zero UNREACHABLE lines
  C2 no reboot or panic      : one boot_id for the whole file, uptime strictly increasing
  C3 starved == 0            : every sample
  C4 zero lock timeouts      : pta_lock_timeouts == 0 in every sample (also stays non-decreasing)
  C5 heartbeat answered      : every sample hb_state is ANSWERED, or OPEN (a challenge issued and not yet answered, inside
                               its D_req window: a normal transient, reported as a share); any other state, or hb_miss > 0, is a FAIL
  C6 traffic keeps flowing   : last_counter rises between every pair of samples
  C7 no alarm events         : counts of TIMEOUT / LATE_ANSWER / AUTH_FAIL / REPLAY / COUNTER_GAP / TRAFFIC_RECOVERED /
                               HEARTBEAT_RECOVERED do not rise after the first sample. No fault was injected during a soak,
                               so every rise is a FALSE POSITIVE candidate. The first sample is the baseline because the
                               start-up artefact (one LATE_ANSWER, one COUNTER_RESYNC) happens before it.
  C8 no ring loss            : ev_lost == 0 at the end
  C9 Core 1 poll age bounded : max poll_age_us <= limit (default 10000 us = 10 ms; typical value is a few hundred us).
                               This limit is a review threshold chosen here, not a figure from the plan.
Exit code 0 only if C1-C9 all PASS.
"""
import csv, sys, statistics

ALARMS = ["n_TIMEOUT", "n_LATE_ANSWER", "n_AUTH_FAIL", "n_REPLAY", "n_COUNTER_GAP",
          "n_TRAFFIC_RECOVERED", "n_HEARTBEAT_RECOVERED"]


def load(path):
    rows, bad = [], 0
    with open(path, newline="") as f:
        for r in csv.reader(f):
            if not r or r[0] == "utc":
                continue
            rows.append(r)
    return rows


def main():
    limit, handoffs, args = 10000, [], []
    it = iter(sys.argv[1:])
    for a in it:
        if a == "--poll-limit-us":
            limit = int(next(it))
        elif a == "--handoff":
            handoffs.append(next(it))
        else:
            args.append(a)
    if not args:
        print(__doc__)
        return 2
    raw = load(args[0])
    hdr = open(args[0]).readline().strip().split(",")
    unreachable = [r for r in raw if len(r) == 2 and r[1] == "UNREACHABLE"]
    data = [dict(zip(hdr, r)) for r in raw if len(r) == len(hdr)]
    malformed = len(raw) - len(unreachable) - len(data)
    if not data:
        print("no usable samples")
        return 1
    res = []

    def verdict(name, ok, detail, review=False):
        tag = "PASS" if ok else ("REVIEW" if review else "FAIL")
        res.append(ok)
        print(f"{tag:6} {name}: {detail}")

    n = len(data)
    t0, t1 = data[0]["utc"], data[-1]["utc"]
    up0, up1 = float(data[0]["uptime_s"]), float(data[-1]["uptime_s"])
    print(f"file {args[0]}\nsamples {n} (+{len(unreachable)} UNREACHABLE, {malformed} malformed)  {t0} .. {t1}  "
          f"Pi uptime {up0:.0f}s .. {up1:.0f}s  covered {(up1 - up0) / 3600:.2f} h\n")

    verdict("C1 no sample lost", not unreachable and not malformed,
            f"{len(unreachable)} UNREACHABLE" + (f" first at {unreachable[0][0]}" if unreachable else "") +
            (f", {malformed} malformed lines" if malformed else ""))

    ids = {d["boot_id"] for d in data}
    ups = [float(d["uptime_s"]) for d in data]
    mono = all(b > a for a, b in zip(ups, ups[1:]))
    verdict("C2 no reboot or panic", len(ids) == 1 and mono, f"boot_ids {sorted(ids)}, uptime increasing: {mono}")

    st = [int(d["starved"]) for d in data]
    verdict("C3 starved == 0", all(s == 0 for s in st), f"max starved {max(st)}")

    lt = [int(d["pta_lock_timeouts"]) for d in data]
    verdict("C4 zero lock timeouts", max(lt) == 0 and all(b >= a for a, b in zip(lt, lt[1:])),
            f"max pta_lock_timeouts {max(lt)}")

    states = {}
    for d in data:
        states[d["hb_state"]] = states.get(d["hb_state"], 0) + 1
    miss = [int(d["hb_miss"]) for d in data]
    verdict("C5 heartbeat answered", set(states) <= {"ANSWERED", "OPEN"} and max(miss) == 0,
            f"states {states} (OPEN = {100.0 * states.get('OPEN', 0) / n:.1f}% of samples, a challenge awaiting its answer), "
            f"max hb_miss {max(miss)}")

    lc = [int(d["last_counter"]) for d in data]
    stalls = [data[i + 1]["utc"] for i in range(n - 1) if lc[i + 1] <= lc[i]]
    rate = (lc[-1] - lc[0]) / (up1 - up0) if up1 > up0 else 0
    verdict("C6 traffic keeps flowing", not stalls,
            f"last_counter {lc[0]} -> {lc[-1]}, mean {rate:.2f} frames/s (sender period 100 ms = 10/s), "
            f"{len(stalls)} pairs without progress" + (f", first at {stalls[0]}" if stalls else ""))

    print()
    rises, hand = [], []
    for k in ALARMS:
        v = [int(d[k]) for d in data]
        for i in range(n - 1):
            if v[i + 1] > v[i]:
                item = (data[i + 1]["utc"], k, v[i + 1] - v[i])
                (hand if any(item[0].startswith(h) for h in handoffs) else rises).append(item)
    base = ", ".join(f"{k[2:]}={data[0][k]}" for k in ALARMS)
    verdict("C7 no alarm events after the first sample", not rises,
            f"baseline at first sample: {base}; {len(rises)} rise(s) = false-positive candidates")
    for t, k, dv in hand:
        print(f"         {t}  {k[2:]} +{dv}   HANDOFF (soak 1 -> soak 2; sender really paused), not counted")
    for t, k, dv in rises[:30]:
        print(f"         {t}  {k[2:]} +{dv}")
    if len(rises) > 30:
        print(f"         ... {len(rises) - 30} more")

    verdict("C8 no ring loss", int(data[-1]["ev_lost"]) == 0, f"ev_lost at end {data[-1]['ev_lost']}")

    pa = [int(d["poll_age_us"]) for d in data]
    s = sorted(pa)
    p99 = s[min(n - 1, int(0.99 * n))]
    verdict("C9 Core 1 poll age bounded", max(pa) <= limit,
            f"min {min(pa)} us, median {int(statistics.median(pa))} us, p99 {p99} us, max {max(pa)} us (limit {limit} us)",
            review=True)

    print("\nOVERALL:", "ALL PASS" if all(res) else "NOT all pass: read the lines marked FAIL / REVIEW; "
          "do not call this a clean soak")
    return 0 if all(res) else 1


if __name__ == "__main__":
    sys.exit(main())
