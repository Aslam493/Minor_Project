# Final presentation: slide script (TEMPORARY, for review before the slides are built)

Audience: Prof. Kolin Paul, IIT Delhi. Presenter: Aslam. Target: short and congested, 8 slides, about 8 to 10 minutes plus the live demo.
Assumptions I made (tell me if wrong): "reference work" = the earlier work and related work the project stands on (midterm baseline), NOT a list of papers;
"max 2" = the work is shown compactly, so I use few current-work slides instead of the 12 now in the artifact.

## 1. What I reviewed
| Source | What it contains | What I take from it |
|---|---|---|
| `Secure_CAN_Gateway_Midterm.pptx` (9 slides) | 3 trust questions (message, validation decision, log), 3 approaches today, boot flow with core 1, "what I'll deliver next": 5 promises | The reference slide. The 5 promises are the scorecard of the final deck. |
| `Minor_report.pdf` (8 pages, July 2026): the ORIGINAL PLAN | "Secure Automotive Event Logging" on the STM32MP157: 5 objectives, 4 contributions, 7 validation checks, key storage in OP-TEE secure storage, hash-chained log in secure flash, critical/normal packet classification, ground-truth re-check in Linux. | The baseline for "planned versus done" (section 2b). |
| `Minor_Project_Steps_1_to_10_Updated.pptx` (10 phases) | OP-TEE build, boot chain, kernel, Buildroot, SD image, boot on the Pi, xtest 0 failed, first Trusted App round trip | One line on the reference slide: "Phases 1 to 10: the platform boots and Linux talks to OP-TEE". |
| Existing artifact deck (12 slides) | Good content and design, too long for "congested" | Reuse the best 5 slides, merge the rest. |
| `FINAL_TEST_REPORT.md` | 31 PASS / 1 FAIL (E2b) / 12 limits; post-flash: E2b fixed, E2c known limit | The honest numbers (see section 3). |
| Live stream demo (new) | Infinite packet stream, colors by the Secure World's own verdict | The demo slide. |

Midterm weaknesses to avoid: three slides were only diagrams with no readable text; the third trust question (log integrity) was never implemented; HMAC was "blocked".

## 2. What was done overall (the story)
1. **Platform:** TF-A + OP-TEE + Linux boot on a Raspberry Pi 3; core 1 is given to the Secure World and hidden from Linux (proven: CPU1 offline, loop counter grows).
2. **Design:** Linux only carries frames. The Secure World asks for frames every 50 ms (up to 32, answer within 40 ms), so a late or missing answer is itself evidence. The forwarder holds no key.
3. **Checks:** ID, HMAC-SHA256, counter (replay, duplicate, conflict, gap), clock (silence 300 ms, heartbeat, late answers). 9 attacks sent on a real two-Pi CAN-FD bus, each caught at the right check.
4. **Speed:** the HMAC "blocker" was a debug option (stack checking): 4.95 ms to 54.6 microseconds per frame (90 times), 640 frames/s.
5. **Loss accounting:** 0 lost up to 640 frames/s and bursts up to about 2,490 frames; above that, every drop is counted at one place and equals the Secure World's gap (18 of 18 runs).
6. **Verification campaign and live demo:** repeatable report with evidence files; a live stream that shows each verdict in color.
7. **Not done (said openly):** tamper-evident log, memory guard (no TZASC on the Pi 3), real key provisioning, separate flood alarm.

## 2b. Planned (minor report) versus done: the honest comparison
| Plan in the minor report | Status in the final system | Say it as |
|---|---|---|
| Board: STM32MP157 | Raspberry Pi 3 B (this is a change; one sentence on why is needed, please tell me) | "Platform: Raspberry Pi 3 B" |
| Objective 1: validate every packet before it influences the vehicle | Done: every frame is judged in the Secure World. It detects but does not block. | Done (detection) |
| Objective 2: detect replay and flood | Replay: done (old, duplicate, conflict, gap). Flood: every drop is counted exactly, but there is no separate flood alarm. | Done / partly |
| Objective 3: keep keys away from Linux | Partly: the key is inside the Secure World image and the forwarder holds none (a key search on its binary found nothing). But it is built into the image, not in OP-TEE secure storage with provisioning. | Partly |
| Objective 4: store events in secure flash | Not built. Events are kept in a Secure World ring in RAM. A design exists (`monitor/AUDIT_LOG_PLAN.md`). | Future |
| Objective 5: hash chain, tamper-evident log | Not built; a forward-secure log is designed. | Future |
| Contribution 1: secure interrupt reception | Built and proven first (GPIO25 as secure FIQ on core 1), then replaced by the Secure World asking for frames, because the Pi 3 has no TZPC to lock the chip. | Changed, with a reason |
| Contribution 2: secure RAM and key management | Partly: secure memory is reserved from Linux in the device tree; the Pi 3 has no TZASC, so no hardware enforcement. | Partly |
| Contribution 3: critical/normal classification, ground-truth re-check | Not part of the final monitor (to confirm with you). | Not done |
| 7 checks: ID, HMAC, flood, tamper, timestamp, counter, replay | ID, HMAC (covers tamper), counter, replay: done. Clock checks (silence 300 ms, heartbeat, late answers) replace the timestamp window. Flood: counted only. | Done with changes |
| Not in the plan, added | Pull design with heartbeat and late-answer detection, 90 times speed-up, exact loss accounting, 32-case verification report, live demo | New |

## 3. Numbers to correct before the deck is final
| Claim in the artifact | Fact in the report | Proposed wording |
|---|---|---|
| "32 of 32 tests pass" (results, sota slides) | Campaign 31 PASS / 1 FAIL / 12 documented limits. E2b (forwarder at boot) failed, was fixed by the service image and now passes. E2c (service control over ssh) is a known limit. | "31 of 32 on the first run; the one failure was fixed and re-tested; 12 limits documented." |
| Category bars "4 of 4, 3 of 3, 10 of 10, 12 of 12, 3 of 3" | Counts must be recomputed from the report rows | Recompute from `results.json` before use. |
| "Midterm figures are estimates" | Correct and already stated | Keep the footnote. |

## 4. Slide by slide
Style: dark title and closing slides, light content slides, one accent color; at most 3 short lines per block, big numbers instead of sentences.

### Slide 1. Intro
- Title: Secure CAN Monitor on Raspberry Pi 3. Subtitle: ARM TrustZone and OP-TEE.
- Name, supervisor, IIT Delhi logo, "Minor Project, Final Evaluation, November 2026".
- One-line claim: "Every frame is judged in the Secure World. Every lost frame is counted."
- Say (15 s): what the project is in one sentence.

### Slide 2. Reference work (the base we started from)
- Left, "Why": 4 attacks (spoofed ID, tampered data, replay, silence); 3 trust questions: the message, the decision, the record.
- Middle, "What exists today": checks in Linux (breaks if Linux is hacked); external HSM (extra hardware); TrustZone + OP-TEE (ours, no extra hardware).
- Far left, "The plan" (minor report, July 2026): 5 objectives in one line each (validate, detect replay and flood, protect keys, secure flash, hash chain) and the planned 7 checks.
- Right, "Where we stood at midterm": phases 1 to 10 done (boots, Linux talks to OP-TEE); core 1 owned by the Secure World; CAN chip, interrupt, first checks proven (3 of 7 milestones); HMAC blocked.
- Bottom strip: the 5 promises (HMAC, latency, flood, real demo, secure log). They return as the scorecard on slide 7.
- Visual: one horizontal timeline, Phases 1 to 10 then September then Midterm.

### Slide 3. Current work 1: the design
- Message: "Linux carries frames, the Secure World judges them."
- Diagram: Sender ECU, CAN chip, kernel socket, forwarder (queue 2,048), Monitor in the Secure World on core 1; loop arrow "request every 50 ms, up to 32 frames, answer in 40 ms".
- Three-box evolution: midterm (Secure World reads the chip; dropped, no TZPC on the Pi 3), push (Linux decides the timing), pull (now).
- Note: the forwarder holds no key (key search on the binary found nothing).

### Slide 4. Current work 2: checks and attacks
- Message: "9 attacks sent on a real bus, 9 caught, each at the right check."
- Table: attack, check that catches it, verdict (MALFORMED, AUTH_FAIL, REPLAY x3, GAP, TIMEOUT x2, LATE).
- Footer: order of checks, cheapest first (ID 6.7 microseconds, then HMAC and counter, 54.6 microseconds per valid frame).

### Slide 5. Current work 3: speed, loss and tests
- Big number left: 90 times faster (4.95 ms to 0.055 ms); cause in one line (debug stack check).
- Middle: 640 frames/s; 0 lost up to bursts of about 2,490 frames; above that, drops counted and equal to the gap (18 of 18 runs).
- Right: test result with the CORRECTED wording from section 3.
- This slide merges the old "speed" and "results" slides.

### Slide 6. Current work 4: live demo
- Message: "Two Pis, one command, every verdict in color."
- Picture: the two terminal windows (sender looks normal and all green; receiver colors each packet: red tampered, orange replay, light blue duplicate, purple conflict, pink wrong ID, yellow gap).
- Right: what the professor sees step by step (attack every 2 to 3 s; the receiver finds it, not the sender).
- Source for the picture: a real captured run, labeled as such.

### Slide 7. Scorecard: planned versus done, limits, future work
- Table: the 5 minor-report objectives (rows from section 2b) with a status mark each: validate done; replay and flood done and partly; keys partly; secure flash and hash chain future. Next to it the 5 midterm promises (HMAC done, latency done, flood partly, real demo done, secure log designed).
- One line: "The platform and the delivery path changed (Raspberry Pi 3, pull design); the checks and the evidence are in place."
- Limits in one line: detects but does not block (no TZASC); keys are built into the image.
- Future: forward-secure audit log, EL2 memory guard, more throughput, key provisioning plus flood alarm.

### Slide 8. Thanks
- "Thank you", one-line claim again, name, supervisor, "Questions welcome".

## 5. If even shorter is needed (6 slides)
Merge slide 3 and slide 4 into one "design and checks" slide, and merge slide 7 into slide 5. Slides 1, 2, 6, 8 stay.

## 6. Decisions for you
0. **New:** are the rows in section 2b correct? Please check the platform change reason, the classification row and the secure-storage row, because I took them from the report and the notes, and you know what was dropped on purpose.
1. Is "reference work" = earlier work (as I assumed) or a list of papers? If papers, add them as a small footer on slide 2 (Schneier and Kelsey, FssAgg, EmLog, MyTEE are already in the future-work slide).
2. Use the corrected test wording from section 3? (My recommendation: yes. The professor can check the report.)
3. Edit the existing artifact (12 slides to 8) or create a new one?
