# Final presentation: slide script (TEMPORARY; decisions made, facts verified, not yet built)

Audience: Prof. Kolin Paul, IIT Delhi. Presenter: Aslam. Deck: **12 slides**, built by editing the existing artifact (a copy of the current version is kept first). Every slide is rendered and checked before the next one.

## 1. The 12 slides (one numbering)
Required course structure: 1 title, 2 motivation and problem, 3 state of the art and promises, 4 work done, 5 video.

| # | Slide | Message | Content (what is folded in) |
|---|---|---|---|
| 1 | Title | Secure CAN Monitor on Raspberry Pi 3 | Name, supervisor, IIT Delhi, "Final Evaluation, November 2026", one-line claim |
| 2 | Motivation and problem | CAN trusts every frame, and Linux can lie about it | 4 attacks; the 3 trust questions (message, decision, record); goal |
| 3 | State of the art and promises | What exists, what we promised, how far we got | 3 approaches on 3 criteria; the 5 midterm promises in the midterm's own words, with status |
| 4 | Work done, previous 1: the secure platform (23 Aug to 8 Sep) | Built by hand, proven on the board | Boot flow, core 1; Steps 1 to 10; xtest "114 test cases, 36,114 subtests, 0 failed"; own Trusted App |
| 5 | Work done, previous 2: CAN in the Secure World (14 to 24 Sep) | First CAN frames checked in the Secure World; HMAC open | CAN chip, GPIO25, secure FIQ on core 1, capture, first checks; 3 of 7 milestones |
| 6 | Work done, current 1: design | Linux carries frames, the Secure World judges them | Diagram; request every 50 ms, 32 frames, 40 ms; how the design changed |
| 7 | Work done, current 2: checks, attacks and timing | 9 attacks, 9 caught, each at the right check | Attack table; **timing values folded in**: 5 timers; B2 321 answers on time over 320 s, 0 late; B3 6,361 requests on time; 8 late answers counted (C9) |
| 8 | Work done, current 3: the speed problem | The HMAC "blocker" was a debug option | **The 4-step search folded in**; 90 times faster; 640 frames/s; 32 frames per request |
| 9 | Work done, current 4: results, loss accounting, trust proofs | Every frame judged or counted; claims backed by a report | **Loss formula and trust proofs folded in**; the 5 category bars (first run); 31 of 32; limits |
| 10 | Video | The Secure World's verdict, live, in color | 30-second recording (you record with `scripts/demo_live_stream.sh`); real still image with the color legend until then; no placeholder text |
| 11 | Future work and limits | What is left, stated openly | Forward-secure log, EL2 memory guard, more throughput, key provisioning plus flood alarm; limits |
| 12 | Thanks | Every frame judged in the Secure World. Every lost frame counted. | Name, supervisor, "Questions welcome" |

## 2. Verified facts (checked on 2026-11-25 against the files; these are the only values the slides may use)
| Claim | Result | Source |
|---|---|---|
| Constant-time tag compare | CONFIRMED: ORs the XOR of all 32 bytes, no early exit | `optee_full/.../plat-rpi3/rpi3_can_monitor.c:53` (function), `:621` (use, 32 bytes) |
| xtest wording | CONFIRMED: "114 test cases, 36,114 subtests, 0 failed" | `README.md:170` |
| 32 original cases, categories A4 / B3 / C10 / D12 / E3 | CONFIRMED (32 cases) | `baseline/final_campaign/results.json` |
| First-run result | 31 PASS, 1 FAIL (E2b). By category: A 4 of 4, B 3 of 3, C 10 of 10, D 12 of 12, **E 2 of 3** | results.json |
| Post-flash | E2b fixed and passes (E2br); FAIL rows E2ar, A2r, B1r each have a passing rerun (E2ar2, A2r2, B1r2); E2c is a known limit (F12), fix queued | results.json, report lines 70 to 90 |
| B2 heartbeat | 321 answers on time, 0 late, over 320 s (forwarder counters). The 311 is only the heartbeat log buffer (full at about 300 s). | `FINAL_TEST_REPORT.md` B2 row |
| B3 | 6,361 requests, all on time | results.json |
| C9 late answers | The forwarder saw 8 LATE and the Secure World counted +8 (this may include one heartbeat) | results.json |
| 9,203 and 80,040 dropped | CONFIRMED as TOTALS over 3 runs: D5c 3 x 14,000 = 42,000 sent, 9,203 dropped; D5a 3 x 35,000 = 105,000 sent, 80,040 dropped | results.json D5c, D5a |
| Frames accounted | 278,204 in D1 to D5e (sum of the series), so "over 270,000" is correct | results.json |
| "18 of 18 runs" | The report's 18 are the load runs of D4 to D5e (6 series x 3). Only 12 of them dropped frames (D4, D5a, D5c, D5e). | report line 10 |
| Lossless limit | 640 frames/s sustained; bursts up to about 2,490 frames at 3,587 frames/s | report PACKET_LOSS_VERDICT |
| Speed | 54.6 microseconds per valid frame, 6.7 microseconds per wrong ID; 1,004-frame burst 1.56 s (median 1.57 at 600 MHz, 1.56 at 1200 MHz, so CPU speed made no difference) | C5b, D1a, D1b |
| 4.95 ms per frame | Measured with instrumentation as "4.0 ms + 4.95 ms per packet" on the debug build (before the fix) | `REAL_BUS_RESULTS.md:200`, `baseline/BURST_STEPS_RESULTS_20261121.md:35` |
| Root cause | CFG_CORE_DEBUG_CHECK_STACKS=y (5,060 hook calls) | `REAL_BUS_RESULTS.md:236` |
| E1 restarts | 10 of 10 as designed | results.json |

## 3. Corrections applied to the existing artifact
1. "32 of 32 tests pass" (results and sota slides) becomes "31 of 32 on the first run; the failure (E2b) was fixed and re-tested; one new limit found after the fix (E2c)".
2. The category bars show the FIRST RUN: A 4/4, B 3/3, C 10/10, D 12/12, **E 2/3**. (Your note says the bars are correct for the 32 cases; the sizes are, but E must show 2 of 3 on the first run, or the bar says 3 of 3 only after the fix and is labeled so.)
3. "18 / 18 overload runs" becomes "18 of 18 load runs; 12 of them dropped frames, and in all 12 the counted drops equal the Secure World's gap".
4. 9,203 and 80,040 are labeled "dropped, total over 3 runs".
5. "Midterm build: 4.95 ms" becomes "Before the fix: 4.95 ms per frame (measured with instrumentation)". It was measured in November, not at the midterm; the other midterm figures (130/s, 8 s, 7 frames) stay labeled as estimates.
6. B2 uses "321 on time, 0 late over 320 s".
7. "8 of 8 late" becomes "8 late answers, counted by both sides".
8. The 5 promises use the midterm's own words ("HMAC authentication", "Reduce validation latency", "Store validated packets in flash memory", "Rate & flood detection", "End-to-end demo with real measurements"); the artifact had paraphrased two of them.
9. "Constant-time check" stays (verified).
10. The demo slide's placeholder line is removed; the slide becomes the video slide.

## 4. Build plan
1. Copy the current artifact (12-slide version) as a backup first.
2. Edit slide by slide in deck order; after each slide render it and check overlaps and text size before the next.
3. The video slide uses a real still image from a captured run with the color legend; you replace it with your 30-second recording.
4. Commit and push this script and the final deck notes.

## 5. Decisions (all made by you; none open)
12 slides; "31 of 32" wording; constant-time kept; xtest and B2 wording as above; loss totals labeled; edit the existing artifact after a backup; you record the video; this file cleaned first.
Open for you only: the E category bar (2 of 3 on the first run, correction 2).
