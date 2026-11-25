# Final presentation: slide script (TEMPORARY, for review before the slides are built)

Audience: Prof. Kolin Paul, IIT Delhi. Presenter: Aslam.

## 0. The structure that must be followed (from the course)
| # | Required part | Our slides |
|---|---|---|
| 1 | Title slide | Slide 1 |
| 2 | Motivation, and the problem we want to solve | Slide 2 |
| 3 | State of the art we want to beat, and what we promised for the end-sem | Slide 3 |
| 4 | Work done | Slides 4 to 11 (previous work = 2 slides, current work = 6 slides) |
| 5 | Video, if any | Slide 12 (the recording of the live stream) |
| - | Closing | Slide 13 (thanks) |

## 1. The 13 slides
| # | Slide | One message | Main content |
|---|---|---|---|
| 1 | Title | Secure CAN Monitor on Raspberry Pi 3 | Name, supervisor, IIT Delhi, "Final Evaluation, November 2026", one-line claim |
| 2 | Motivation and problem | CAN trusts every frame, and Linux can lie about it | 4 attacks (spoofed ID, tampered data, replay, silence); the 3 trust questions (the message, the decision, the record); goal in one line |
| 3 | State of the art and our promise | What exists, what we promised, how far we got | Left: 3 approaches today compared on 3 criteria. Right: the 5 midterm promises with status (HMAC done, latency done, flood partly, real demo done, secure log designed). |
| 4 | Work done, previous 1: the secure platform (23 Aug to 8 Sep) | I built the secure platform by hand and proved it on the board | Boot flow, core 1 goes to the Secure World; Steps 1 to 10; xtest 114 cases 0 failed; own Trusted App |
| 5 | Work done, previous 2: CAN in the Secure World (14 to 24 Sep) | The first CAN frames were checked in the Secure World; HMAC was open | CAN chip, GPIO25, secure FIQ on core 1, capture, first checks; 3 of 7 milestones at midterm |
| 6 | Work done, current 1: design | Linux carries frames, the Secure World judges them | Design diagram (request every 50 ms, up to 32 frames, answer within 40 ms); how the design changed from midterm |
| 7 | Work done, current 2: checks and attacks | 9 attacks on the real bus, 9 caught, each at the right check | Attack, check, verdict table; order of checks |
| 8 | Work done, current 3: timing | The Secure World keeps the time, so silence and delay become evidence | The five timers; measured: 5 minutes of heartbeat 0 late, 6,361 requests on time, late answers 8 of 8 detected |
| 9 | Work done, current 4: the speed problem | The HMAC "blocker" was a debug option | 4 steps of the search; 90 times faster; 640 frames/s; 32 frames per request |
| 10 | Work done, current 5: loss accounting | Every frame is judged or counted | The accounting formula; 0 lost up to 640 frames/s and bursts of about 2,490 frames; 18 of 18 overload runs exact |
| 11 | Work done, current 6: verification, limits, future | Claims are backed by a test report; limits are stated | 31 of 32 on the first run (corrected), 12 limits; what Linux cannot see; future work strip |
| 12 | Video | The verdict of the Secure World, live, in color | The 30-second recording of the two windows, with a real still image and the color legend |
| 13 | Thanks | Every frame judged in the Secure World. Every lost frame counted. | Name, supervisor, "Questions welcome" |

If 13 is too long: drop slide 8 first, merge slide 10 into slide 9, merge slide 11 into slide 3 (the scorecard already holds the status). That gives 10 slides.

## 2. Review of the 12 slides now in the artifact (content read; layout NOT yet rendered)
| Existing slide | What it holds | Problems found | Improvement |
|---|---|---|---|
| cover | Title, claim, name, supervisor | None | Keep |
| problem | 4 attack cards, goal | The third trust question (the record) is missing, though the midterm asked all three | Add the 3 trust questions as a one-line strip; link the third to future work |
| sota | 3 approaches; 5 promises with status | (a) "32 of 32 tests pass" is not what the report says (31 PASS, 1 FAIL, fixed later); (b) "constant-time check" is not backed by anything I verified; (c) "to beat" is vague | (a) use the corrected wording; (b) remove it or verify it first; (c) compare the 3 approaches on 3 clear criteria: works when Linux is compromised, needs extra hardware, counts what it lost |
| sept1 | Boot chain, cores | Good; no dates; the xtest evidence is missing | Add dates (23 Aug to 8 Sep) and the evidence strip: Steps 1 to 10, xtest 114 cases 0 failed, own Trusted App |
| sept2 | CAN chip to checks, 3 of 7 | Good | Add one arrow "changed later, see slide 6" so the story is continuous |
| arch | Design diagram, 3-box evolution | Built with absolute positions; overlaps unchecked | Render and check each box; make sure the forwarder is labeled "no key" |
| attacks | 9 attacks as a wall diagram | Dense (10 rows); small text risk | Render; keep only if every label is readable, else use a plain table |
| perf | 90 times, bars, three tiles | Good; the midterm estimates are footnoted | Keep; add the 4-step search so the process shows |
| results | 32 of 32, bars, loss bars | **Wrong test count**; category counts (4 of 4, 3 of 3, 10 of 10, 12 of 12, 3 of 3) must be recomputed; loss figures (9,203 and 80,040) must be checked against `results.json` | Recompute everything from `baseline/final_campaign/results.json` |
| demo | Terminal sketch and 6-step table | A placeholder line is still on it ("add recording link here"); the steps are outdated (the live stream replaced them) | Replace with the Video slide: recording plus a real still |
| future | 4 cards | Good | Shrink into a strip on slide 11 |
| close | Thank you | Good | Keep |

Layout check: I have not rendered the existing slides. Every slide I build is rendered and checked before the next one.

## 3. Numbers to correct before the deck is final
| Claim in the artifact | Fact in the report | Proposed wording |
|---|---|---|
| "32 of 32 tests pass" (results and sota slides) | Campaign 31 PASS / 1 FAIL / 12 documented limits. E2b (forwarder at boot) failed, was fixed by the service image and now passes. E2c (service control over ssh) is a known limit. | "31 of 32 on the first run; the one failure was fixed and re-tested; 12 limits documented." |
| Category bars | Counts must be recomputed from the report rows | Recompute from `results.json`. |
| Loss figures | Check each against the result files | Verify when the slide is built. |

## 4. The previous work in two slides (what is shown)
- Slide 4 (23 Aug to 8 Sep): Steps 1 to 7 built OP-TEE, TF-A, U-Boot, kernel and root file system; SD image verified; Steps 8 to 10 booted on the real Pi, OP-TEE 4.10 answers, xtest 114 cases with 0 failures, own Trusted App round trip.
- Slide 5 (14 to 24 Sep): CAN shield (MCP2518FD), simulated then real CAN pipeline, Secure World checks and classification, the chip interrupt reaching the Secure World, core 1 reserved for the Secure World; midterm: 3 of 7 milestones proven, HMAC blocked. Dead ends (a failed boot-flow attempt, a secure-timer experiment) are one line: rolled back to a known-good image.

## 5. The video (slide 12)
- You record about 30 seconds of the two windows (`bash scripts/demo_live_stream.sh`); I place the file on the slide.
- Fallback if the video cannot play: a still image of the two windows made from a real captured run, labeled as such, plus the color legend.
- The video must show: green packets, then one colored attack at a time on the receiver, and the sender staying green.

## 6. Decisions for you
1. Is the 13-slide structure above right, or do you want the 10-slide version?
2. Do you accept "31 of 32" in place of "32 of 32"? (The report shows one FAIL; the professor can check.)
3. Edit the existing artifact (keeping a copy of the 12-slide version first) or create a new one? I recommend editing it.
4. The video: will you record it, or should I make the still image first?
