# Final presentation: slide script (TEMPORARY, for review before the slides are built)

Audience: Prof. Kolin Paul, IIT Delhi. Presenter: Aslam. Target: short and congested, 9 slides, about 10 minutes plus the live demo.

Structure you asked for: slide 1 intro, slides 2 and 3 the previous work (MAXIMUM 2 slides, only the important work: the MINOR WORK implemented in August and September), slides 4 to 8 the current work, slide 9 thanks.

## 1. What I reviewed
| Source | What it contains | What I take from it |
|---|---|---|
| `Minor_Project_Steps_1_to_10_Updated.pptx`, `Minor_Project_5_Phase_Implementation.pptx` | The minor work, Steps 1 to 10: OP-TEE, TF-A, U-Boot, kernel, Buildroot, SD image, boot on the Pi, xtest, first own Trusted App | Left half of slide 2 |
| Git history 23 Aug to 17 Sep 2026 (35 commits) | CAN shield, simulated then real CAN pipeline, Secure World frame verification and classification, secure interrupt, parallel cores, incidents | Right half of slide 2 |
| `Secure_CAN_Gateway_Midterm.pptx` (9 slides) | 3 trust questions, 3 approaches today, boot flow with core 1, 5 promises for the final | Problem part of slide 2 and the scorecard on slide 7 |
| Existing artifact deck (12 slides) | Good content and design, too long for "congested" | Reuse the best slides, merge the rest |
| `FINAL_TEST_REPORT.md` | 31 PASS / 1 FAIL (E2b) / 12 limits; post-flash: E2b fixed, E2c known limit | The honest numbers (section 3) |
| Live stream demo (new) | Infinite packet stream, colors from the Secure World's own verdict | Demo slide |
| `Minor_report.pdf` (July 2026) | The first PLAN, written for the STM32MP157 board | Not used for the story (you said the minor work is the August and September implementation). Could be one footnote if the professor asks. |

Midterm weaknesses to avoid: three slides were only diagrams with no readable text; the third trust question (log integrity) was never implemented; HMAC was "blocked".

## 2. What was done overall (the story)
1. **Minor work, Aug and Sept (earlier work):** the whole secure platform built by hand: OP-TEE, TF-A, U-Boot, kernel and root file system on a Raspberry Pi 3; booted on the real board; Linux talks to OP-TEE (xtest: 114 cases, 0 failed); own Trusted App; then CAN hardware (Seeed shield, MCP2518FD), Secure World frame checks and classification, the chip interrupt reaching the Secure World, core 1 reserved for the Secure World.
2. **Design (current work):** Linux only carries frames. The Secure World asks for frames every 50 ms (up to 32, answer within 40 ms), so a late or missing answer is itself evidence. The forwarder holds no key.
3. **Checks:** ID, HMAC-SHA256, counter (replay, duplicate, conflict, gap), clock (silence 300 ms, heartbeat, late answers). 9 attacks sent on a real two-Pi CAN-FD bus, each caught at the right check.
4. **Speed:** the HMAC "blocker" was a debug option (stack checking): 4.95 ms to 54.6 microseconds per frame (90 times), 640 frames/s.
5. **Loss accounting:** 0 lost up to 640 frames/s and bursts up to about 2,490 frames; above that, every drop is counted at one place and equals the Secure World's gap (18 of 18 runs).
6. **Verification campaign and live demo:** repeatable report with evidence files; a live stream that shows each verdict in color.
7. **Not done (said openly):** tamper-evident log, memory guard (no TZASC on the Pi 3), real key provisioning, separate flood alarm.

## 3. Numbers to correct before the deck is final
| Claim in the artifact | Fact in the report | Proposed wording |
|---|---|---|
| "32 of 32 tests pass" (results and sota slides) | Campaign 31 PASS / 1 FAIL / 12 documented limits. E2b (forwarder at boot) failed, was fixed by the service image and now passes. E2c (service control over ssh) is a known limit. | "31 of 32 on the first run; the one failure was fixed and re-tested; 12 limits documented." |
| Category bars "4 of 4, 3 of 3, 10 of 10, 12 of 12, 3 of 3" | Counts must be recomputed from the report rows | Recompute from `results.json` before use. |
| Midterm figures except 4.95 ms are estimates | Correct and already footnoted | Keep the footnote. |

## 4. Slide by slide
Style: dark title and closing slides, light content slides, one accent color; at most 3 short lines per block, big numbers instead of sentences.

### Slide 1. Intro
- Title: Secure CAN Monitor on Raspberry Pi 3. Subtitle: ARM TrustZone and OP-TEE.
- Name, supervisor, IIT Delhi logo, "Minor Project, Final Evaluation, November 2026".
- One-line claim: "Every frame is judged in the Secure World. Every lost frame is counted."

### Slide 2. Previous work 1: the secure platform (Aug to early Sep) - the important part only
- Message: "I built the whole secure platform by hand, and proved it on the real board."
- Left, the problem in one line each: 4 attacks (spoofed ID, tampered data, replay, silence); 3 trust questions (the message, the decision, the record); options today (checks in Linux, external HSM) versus TrustZone on the same chip.
- Main, one flow diagram: Boot ROM, GPU firmware, TF-A (BL1, BL2, BL31 at EL3), OP-TEE (Secure World), U-Boot, Linux. Highlight: when Linux asks to start CPU1, BL31 sends core 1 into the Secure World for good; Linux keeps cores 0, 2, 3.
- Evidence strip (3 numbers): Steps 1 to 10 complete; xtest 114 cases, 0 failed; own Trusted App round trip passed.
- Dates: 23 Aug to 8 Sep.

### Slide 3. Previous work 2: CAN in the Secure World, and where midterm ended (14 Sep to 24 Sep)
- Message: "The first CAN frames were checked in the Secure World; HMAC was the open problem."
- Chain of boxes with a "proven" mark: MCP2518FD CAN chip (Seeed shield) to GPIO25 interrupt to secure FIQ on core 1 to capture into secure memory to checks (ID, counter, timestamp continuity).
- Left card: 3 of 7 milestones proven at midterm; HMAC blocked (Secure World slow).
- Right card: the 5 promises for the final (HMAC, latency, flood, real demo, secure log). They return as the scorecard on slide 8.
- One line: dead ends were rolled back to a known-good image (failed boot-flow attempt, secure-timer experiment).

### Slide 4. Current work 1: the design
- Message: "Linux carries frames, the Secure World judges them."
- Diagram: Sender ECU, CAN chip, kernel socket, forwarder (queue 2,048), Monitor in the Secure World on core 1; loop arrow "request every 50 ms, up to 32 frames, answer in 40 ms".
- Three-box evolution: midterm (Secure World reads the chip; dropped, no TZPC on the Pi 3), push (Linux decides the timing), pull (now).
- Note: the forwarder holds no key (a key search on its binary found nothing).

### Slide 5. Current work 2: checks and attacks
- Message: "9 attacks sent on a real bus, 9 caught, each at the right check."
- Table: attack, the check that catches it, the verdict (MALFORMED, AUTH_FAIL, REPLAY x3, GAP, TIMEOUT x2, LATE).
- Footer: order of checks, cheapest first (ID 6.7 microseconds, then HMAC and counter, 54.6 microseconds per valid frame).

### Slide 6. Current work 3: speed, loss and tests
- Big number left: 90 times faster (4.95 ms to 0.055 ms); the cause in one line (debug stack check).
- Middle: 640 frames/s; 0 lost up to bursts of about 2,490 frames; above that the drops are counted and equal the gap (18 of 18 runs).
- Right: test result with the CORRECTED wording from section 3.

### Slide 7. Current work 4: live demo
- Message: "Two Pis, one command, every verdict in color."
- Picture: the two terminal windows (sender looks normal and all green; receiver colors each packet: red tampered, orange replay, light blue duplicate, purple conflict, pink wrong ID, yellow gap).
- Right: what the professor sees (an attack every 2 to 3 s; the receiver finds it, not the sender).
- The picture comes from a real captured run and is labeled as such.

### Slide 8. Scorecard, limits and future work
- Table of the 5 midterm promises with status: HMAC done; latency done; flood partly (counted, no alarm); real demo done; secure log not built (designed).
- Limits in one line: detects but does not block (no TZASC); keys are built into the image.
- Future: forward-secure audit log, EL2 memory guard, more throughput, key provisioning plus flood alarm.

### Slide 9. Thanks
- "Thank you", the one-line claim again, name, supervisor, "Questions welcome".

## 5. If even shorter is needed (7 slides)
Merge slides 4 and 5 into one "design and checks" slide, and merge slide 8 into slide 6. Slides 1, 2, 3, 7, 9 stay.

## 6. Decisions for you
1. Are slides 2 and 3 (the minor work: first the secure platform, then CAN in the Secure World with the midterm position) the right 2 slides of previous work?
2. Use the corrected test wording from section 3? (My recommendation: yes. The professor can check the report.)
3. Edit the existing artifact (12 slides to 8) or create a new one? (My recommendation: edit it.)
