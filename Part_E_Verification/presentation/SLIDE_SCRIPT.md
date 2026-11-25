# Final presentation: slide script (TEMPORARY, for review before the slides are built)

Audience: Prof. Kolin Paul, IIT Delhi. Presenter: Aslam. Target: short and congested, 8 slides, about 8 to 10 minutes plus the live demo.

Structure you asked for: slide 1 intro, slides 2 and 3 previous work (maximum 2, only the important work: the MINOR WORK of August and September), slides 4 to 7 current work (4 slides, with the mandatory values), slide 8 thanks.

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
- Message: "Linux carries frames, the Secure World judges every one."
- Diagram: Sender ECU, CAN chip, kernel socket, forwarder (queue 2,048), Monitor in the Secure World on core 1; loop arrow "request every 50 ms, up to 32 frames, answer within 40 ms". The forwarder holds no key.
- Three-box evolution: midterm (Secure World reads the chip; dropped because the Pi 3 has no TZPC to lock the chip), push (Linux decides the timing; not enough), pull (now: Linux cannot pick the timing, a late or missing answer is evidence).
- Big fact: heartbeat every 1 s, 3 misses raise TIMEOUT; boot grace 60 s.

### Slide 5. Current work 2: checks and attacks
- Message: "9 attacks sent on the real two-Pi CAN-FD bus, 9 caught, each at the right check."
- Table: attack, the check that catches it, the verdict (wrong ID: MALFORMED; tampered data: AUTH_FAIL; old, duplicate, conflicting counter: REPLAY x3; counter jump: GAP; silence 300 ms and killed forwarder: TIMEOUT x2; late answers: LATE).
- Footer: checks run cheapest first (ID 6.7 microseconds, then HMAC-SHA256 and counter, 54.6 microseconds per valid frame).

### Slide 6. Current work 3: results (the mandatory values), scorecard and limits
- Message: "Fast enough, no frame lost silently, and every claim is backed by a test report."
- Big numbers:
  1. **90 times faster**: 4.95 ms to 0.055 ms per frame (cause: a debug stack check, removed).
  2. **640 frames/s** judged, 32 frames per 50 ms request.
  3. **0 lost** up to 640 frames/s and bursts of about 2,490 frames.
  4. **18 of 18** overload runs: counted drops equal the Secure World's gap (over 270,000 frames accounted).
  5. **31 of 32** cases on the first run (corrected wording); the one failure fixed and re-tested; 12 limits documented.
- Small strip: the 5 midterm promises with status (HMAC done; latency done; flood partly, counted but no alarm; real demo done; secure log designed, not built).
- Bottom line: limits (detects but does not block, no TZASC; keys built into the image) and future work (forward-secure log, EL2 memory guard, key provisioning, flood alarm).

### Slide 7. Current work 4: live demo
- Message: "Two Pis, one command, every verdict in color."
- Picture: the two terminal windows (sender looks normal and all green; receiver colors each packet: red tampered, orange replay, light blue duplicate, purple conflict, pink wrong ID, yellow gap), taken from a real captured run and labeled as such.
- Right: what the professor sees (an attack every 2 to 3 s; the receiver finds it, not the sender); the one command.

### Slide 8. Thanks
- "Thank you", the one-line claim again ("Every frame judged in the Secure World. Every lost frame counted."), name, supervisor, "Questions welcome".

## 5. Where the cut material goes
The loss study details and the speed story go into the speaker notes of slides 4 to 6; the live stream is slide 7 and the live demo itself. If the professor asks for more, the full 12-slide artifact deck can be kept as a backup copy.

## 6. Decisions for you
1. Are slides 2 and 3 (the secure platform; then CAN in the Secure World with the midterm position) the right 2 slides of previous work, and are the 5 values on slide 5 the ones you want shown?
2. Use the corrected test wording from section 3? (My recommendation: yes. The professor can check the report.)
3. Edit the existing artifact (12 slides to 8) or create a new one? (My recommendation: edit it.)
