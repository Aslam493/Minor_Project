# Final verification campaign: predictions written BEFORE the runs (2026-11-22)
Rules: no flash, no Secure World change, no test or threshold changed; read-only checks contain zero writes; on a FAIL I stop and report before changing anything. Runner: `scripts/final_campaign/campaign.py` (stages R A B C D E1), report: `scripts/final_campaign/make_report.py` -> `FINAL_TEST_REPORT.md`. Order: R (reboot both Pis; A4, E2, then A1) -> A (A1 A2 A3, A2 on the fresh boot because board_full_check E11 is timing-sensitive and passes on an empty event ring) -> B -> C -> D -> E1 -> F (document only).
| ID | Prediction | What would be a surprise |
|---|---|---|
| A1 | verify_boot 17 pass, 0 FAIL, 1 skipped (as on 2026-11-21) | any FAIL |
| A2 | 0 FAIL on the fresh boot; the frozen-image item T21d may be reported KNOWN (not FAIL) | a FAIL in E (detection) or C (isolation) |
| A3 | PASS: the image DTB sha256 e8ac38fa... (compose read-back) and `reserved-memory/optee_secure@10000000` live | node missing |
| A4 | PASS on both: receiver by S12can + canfdcfg in the image, sender by its systemd unit; both come up FD 500000/2000000 | a Pi up in classic mode |
| E2a | PASS: hb_miss_count already >= 1 at the first read (no forwarder yet), TIMEOUT counter 0 inside G = 60 s, forwarder started inside G, still TIMEOUT 0 | a TIMEOUT before 60 s |
| E2b | **FAIL (a gap)**: no init script starts `can_forwarder`; it is run by hand from /tmp (RAM). Reported, not fixed | a service exists |
| B1 | PASS: 600 judged, 0 loss at every stage | any loss |
| B2 | PASS: about 300 heartbeat answers, 0 late after the first, hb_miss_count 0; the first answer may be LATE only if its first state was MISSED | late answer in the middle |
| B3 | PASS: about 5900 requests all ON_TIME (the first may be LATE only at a restart-phase MISSED), 0 STALE | late in the middle |
| C1 | REPLAY +1 detail 1, other attack classes 0, 12 frames judged, last_counter b+2 | detail other than 1 |
| C2 | REPLAY +1 detail 2 (DUPLICATE) | detail other than 2 |
| C3 | REPLAY +1 detail 3 (CONFLICT) | detail other than 3 |
| C4 | AUTH_FAIL +1 | any ACCEPTED |
| C5 | MALFORMED +1 detail 3 (the planted wrong-id frames of the burst tests showed detail 3) | detail other than 3 |
| C5b | wrong-id frames cost less per packet than valid frames (no HMAC): about 1 us against about 22-46 us | wrong id as expensive as valid |
| C6 | COUNTER_GAP +1 detail 4 (b -> b+5), last_counter b+7 | detail other than 4 |
| C7 | silence 1.5 s: traffic age > 300 ms, TIMEOUT detail 1 +1 exactly; 3 more frames: TRAFFIC_RECOVERED +1 | TIMEOUT count not 1 |
| C8 | forwarder killed: hb_miss_count reaches 3, TIMEOUT detail 2 raised; restart: HEARTBEAT_RECOVERED +1 | no TIMEOUT detail 2 |
| C9 | 40 freezes of 30-90 ms: about 20 % of the resumes see a MISSED request, so LATE about 8 (range 3-14); forwarder LATE = Secure World LATE; STALE possibly 0 (the window between fetch and answer is microseconds) | LATE 0 |
| D1a/b | 5 + 5 bursts of 1004: identity PASS, window 1.56-1.62 s at powersave, 1.55-1.61 s at performance | any loss |
| D2 | 30 of 30 PASS, window 0.76 s median | |
| D3 | 5 of 5 PASS, window 3.13 s | |
| D4 | 3 of 3 PASS, judged about 5630, gap = drops (14376-14381) | |
| D5 | flood 3 of 3 PASS (8320 judged, 26680 dropped); 480 frames/s lossless; 943 frames/s counted drops about 3070; 2400 lossless, 2600 loses 72-104 | |
| D6 | documentation: sender txqueuelen 2000 is a RUNTIME setting (after the reboot it is 10 again until set; a gap), chip RX FIFO from `ethtool -g`/dmesg, SO_RCVBUF 8388608 bytes, batch 32, queue 2048 | |
| E1 | 10 of 10: first-answer LATE <=> first state MISSED for the heartbeat and for the packet request, later heartbeat answers all on time | a mismatch |
| F | documented limits (not tests): `ev_lost`/ack, key held by Linux in this prototype, event list rate limit, sender txqueuelen not persistent, forwarder not a service, first answer LATE after a restart, late reply at the harness snapshot | |
Risks I cannot exclude: a reboot can change DHCP addresses (then I stop and ask for the address); the receiver may take longer than 4 minutes to answer; A2's frame tests need the prototype key file copied to /tmp (RAM) and removed afterwards.
