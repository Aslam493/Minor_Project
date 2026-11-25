# Final presentation: slide script (BUILT and published 2026-11-25; 12 slides, one numbering)

Artifact: https://claude.ai/artifact/Up8TZUmsA46W8itm982hzX (edited in place). Backup of the old 12-slide version: `artifact_backup_12slides_20261125/`. Final files: `final_deck_20261125/`. Still image: `video_still_real_run_20261125.png`.
Audience: Prof. Kolin Paul, IIT Delhi. Presenter: Aslam. Required structure: title, motivation and problem, state of the art plus promises, work done, video.

## 1. The 12 slides (id, content, one-line check)
| # | id | Slide | One-line check (rendered with headless Chrome, content ends above y 920) |
|---|---|---|---|
| 1 | cover | Title | Unchanged; claim and names present |
| 2 | problem | Motivation and problem | 4 attacks plus the 3 trust questions (record = future work); fits, ends y 900 |
| 3 | sota | State of the art and promises | 3 approaches; the 5 midterm promises in the midterm's words and order, statuses Done / Done / Not built / Partly / Done; "31 of 32" |
| 4 | sept1 | Previous work 1 (23 Aug to 8 Sep) | Boot chain and cores; evidence strip: Steps 1 to 10, xtest "114 test cases, 36,114 subtests, 0 failed", own Trusted App; ends y 912 |
| 5 | sept2 | Previous work 2 (14 to 24 Sep) | CAN chain, checks, 3 of 7 milestones; HMAC "blocked" points to slide 8; "changed later, see slide 6" |
| 6 | arch | Design | Diagram, forwarder "holds no key", request label inside the lane; 3-box evolution |
| 7 | attacks | Checks, attacks and timing | 9 attacks and verdicts; timing folded in (321 on time over 320 s, 6,361 requests on time, 8 late answers counted by both sides); ends y 912 |
| 8 | perf | The speed problem | 4-step search; "Before the fix (debug build)"; measured 9.9 s and 101/s; ends y 920 |
| 9 | results | Results, loss accounting, trust proofs | First-run bars A 4/4, B 3/3, C 10/10, D 12/12, E 2/3 = 31/32 with the E note; loss totals labeled "over 3 runs"; formula; 18 of 18 load runs, 12 dropped frames; 278,204; three trust proofs; ends y 878 |
| 10 | video | Video | Real still image (25 Nov), color code, what to watch; no placeholder text; replace the still by the 30-second recording |
| 11 | future | Future work and limits | 4 cards (card 1 = midterm promise 3); limits incl. E2c; ends y 910 |
| 12 | close | Thanks | Unchanged |

## 2. Verified facts (checked 2026-11-25; these are the only values the slides use)
| Claim | Result | Source |
|---|---|---|
| Constant-time tag compare | CONFIRMED: ORs the XOR of all 32 bytes, no early exit | `optee_full/.../rpi3_can_monitor.c:53`, used at `:621` |
| xtest wording | "114 test cases, 36,114 subtests, 0 failed" | `README.md:170` |
| 32 original cases | A4 / B3 / C10 / D12 / E3. First run: 31 PASS, 1 FAIL (E2b); **E is 2 of 3** | `results.json` |
| Post-flash | E2b fixed (E2br PASS); E2ar, A2r, B1r FAIL rows have passing reruns; E2c known limit (F12), fix queued | `results.json`, report |
| B2 heartbeat | 321 answers on time, 0 late, over 320 s (the 311 is the log buffer size) | report B2 row |
| B3 | 6,361 requests, all on time | `results.json` |
| C9 late answers | forwarder LATE 8, Secure World +8 (may include one heartbeat) | `results.json` |
| 9,203 and 80,040 | Totals over 3 runs (D5c 42,000 sent; D5a 105,000 sent) | `results.json` |
| Frames accounted | 278,204 in D1 to D5e | `results.json` |
| "18 of 18" | Load runs of D4 to D5e; 12 of them dropped frames (D4, D5a, D5c, D5e) | report line 10 |
| Lossless limit | 640 frames/s; bursts up to about 2,490 frames at 3,587 frames/s | report verdict |
| Speed after the fix | 54.6 microseconds per valid frame; 6.7 per wrong ID; 1,004-frame burst 1.56 s | C5b, D1a |
| 4.95 ms | Before the fix, at 600 MHz: reply = 4.0 ms + 4.95 ms per packet | `REAL_BUS_RESULTS.md:200` |
| **Before the fix, measured** | 1,004-frame drain 9.9 s (101 frames/s) at 600 MHz; reply p99 58 ms | `baseline/BURST_STEPS_RESULTS_20261121.md` Step 2d |
| **CPU clock before the fix** | 1200 MHz was 4.6 times faster per packet (4.95 to 1.07 ms) and the drain 9.9 s to 2.0 s: a clue, not the cause | same file, Step 2d |
| CPU clock after the fix | No difference: D1a 1.57 s (600 MHz) versus D1b 1.56 s (1200 MHz) | `results.json` |
| Root cause | CFG_CORE_DEBUG_CHECK_STACKS=y, 5,060 hook calls, 45 to 100 times slower | `REAL_BUS_RESULTS.md:236` |
| Option in the midterm build | The core built on 2026-09-20 contains the hook functions; the 2026-08-30 core does not. Likely reason HMAC looked too slow at midterm (inference; confirmed measurements are from November) | `nm` of the saved `tee.elf` files |
| **No key in the forwarder (re-run 25 Nov)** | `can_forwarder` sha256 36ee2d58b8ff: keys inside NONE; `mon_client` 79d91eb32cd0: NONE; boot partition not mounted in Linux | `scripts/demo_prove_no_key.py` |
| Core 1 hidden (25 Nov) | Linux online 0,2-3, offline 1; "CPU1: failed to come online"; core 1 loop counter +39,536,094 in 1 s | `scripts/demo_cores.py` |
| Secure memory reserved (25 Nov) | `/proc/iomem`: 08000000-09ffffff and 10000000-10ffffff reserved | receiver |

## 3. Corrections made to the previous artifact
1. "32 of 32" removed everywhere; "31 of 32 on the first run".
2. Category bars show the first run (E 2 of 3, with a note).
3. "18 / 18 overload runs" became "18 of 18 load runs; in the 12 that dropped frames, counted drops = the gap; 278,204 frames accounted".
4. Loss figures labeled as totals over 3 runs.
5. "Midterm build" became "Before the fix (debug build)".
6. "CPU speed made no difference" was WRONG as a step in the search (before the fix the clock helped 4.6 times); it is now step 2, "a clue, not the cause", and the "no difference" statement applies only after the fix.
7. The estimates "about 8 s" and "about 130/s" became the measured 9.9 s and 101/s; "7 frames" is labeled calculated.
8. "8 of 8 late" became "8 late answers, counted by both sides".
9. Promises use the midterm's own words; promise 3 is "Not built; designed as a forward-secure log".
10. The demo slide's placeholder is gone; it is the video slide.

## 4. Open items for the presenter
- Record the 30-second video with `bash scripts/demo_live_stream.sh` and replace the still on slide 10.
- The preview used fallback fonts (Arial-like); the deck uses Inter and Space Grotesk, which are slightly wider. Slides 3, 8 and 9 have the least slack; check them once in the real viewer.
- Pictures (the IIT Delhi logo) do not show in my preview; they are in the published deck.

## 5. Update 2026-11-25 (second pass: less content, bigger video)
- Every content slide was trimmed to fewer words; detail moved into the speaker notes. Text sizes were raised where space was freed (slides 2, 3, 6, 8, 9, 11).
- Slide 10 now shows the video full width (1664 px), re-rendered from the same real recording with 1.4 times larger text and cropped to the useful rows; the closing caption now shows; the spinner glyph was replaced because the font had no braille glyphs.
- The user had widened the video by hand in the editor (939 px). That edit lived only on the old layout; the new layout replaces it.
- A page save from the editor happened between two of my publishes; the publish replaced 10 slide files with my local copies. If other hand edits were made in those files, they need to be redone.

## 6. Update 2026-11-25 (third pass: full-slide video)
- Slide 10 is now a medium heading plus the video filling the rest of the slide (1920 x 994 frame, 60 s, 12.5 MB), dark background, no footer. The video was re-rendered taller from the same real recording: sender 5 dashboard rows + 4 packet lines, receiver 5 dashboard rows + 14 packet lines + the Secure World counters line, caption bar at the bottom.
- Before this publish the live artifact had a newer version saved from the editor (the user had moved the heading and made the video 796 px high by hand). Only `video.html` and `deck.css` differed from my copies; the CSS had no user change; the new layout replaces the video-slide edit, which pointed in the same direction.

## 7. Update 2026-11-25 (one color)
- All 12 slides now use the same light background. The cover, closing and video slides were dark; they are light now (cover name and institute text recolored for contrast).
- The small IIT Delhi logo is removed from the first and last slides. The big faint logo copy was near-white (made for the dark background), invisible on light, so it was removed too.

## 8. Update 2026-11-25 (background logo)
- The first and last slides each carry ONE large IIT Delhi seal as a background (blue, 14 percent opacity baked into the picture, behind the text, right side). The old logo file is near-white (made for the dark theme) and is invisible on light, so a blue copy was made: `logo_background_blue.png`.

## 9. Update 2026-11-25 (slide 11 redesigned: secure log in flash)
- Slide 11 is now about the user's next work item: a secure log in flash (midterm promise 3). It follows `monitor/AUDIT_LOG_DESIGN.md` and `AUDIT_LOG_PLAN.md`: Secure World seals each record (chain tag = HMAC(K_log, previous tag + record)), a Linux logger carries it to the SD card (the only flash on a Pi 3), the newest tag is also copied to the UART as an outside anchor; three protection levels (software, an extra flash chip, a board with a memory firewall).
- Animation: the chain builds E1 to E5, then E3 turns red ("edited by Linux") and E4, E5 show "chain broken".
- Honest limits on the slide and in the notes: edits and middle deletions are detected; deleting the end needs the UART copy; a rolled-back SD card cannot be detected without an outside anchor; status "Designed, not built".
- OPEN WORDING POINT: slide 3 (as dictated) says "designed as a forward-secure log", but the design documents describe a plain HMAC chain (K_log, UART anchor). Forward secrecy (key moves on after each entry) is a stronger option beyond the design and is mentioned that way in the notes. Decide whether slide 3 should say "tamper-evident HMAC-chained log" instead.

## 10. Update 2026-11-25 (slide 11 title and bullets)
- Slide 11 title is "Future work" (small label above: secure log in flash, midterm promise 3, designed not built). The three level boxes were removed (the levels are in the speaker notes); three plain bullets were added: build the log (seal in Secure World, store in flash), close the gap (UART copy of the newest tag), after that (EL2 memory guard, real keys, flood alarm).

## 11. Update 2026-11-25 (slide 11 is a pure bullet list, no boxes)
- Slide 11 "Future work" is now plain bullet points with sub-bullets and no boxes: (1) secure log in flash (promise 3, designed, not built): seal in the Secure World with tag(i) = HMAC(K_log, tag(i-1) + record(i)), Linux only carries the records to the SD card, UART copy of the newest tag; (2) memory guard at EL2; (3) real keys; (4) flood alarm and more throughput; one line of limits at the bottom. The animated chain and the level boxes were removed; their detail stays in the speaker notes.

## 12. Update 2026-11-25 (slide 11 text balanced)
- Slide 11 text was trimmed and balanced: one main item with three short sub-points (secure log in flash: each record sealed in the Secure World (HMAC chain); Linux carries the records to the SD card; newest tag copied to the UART), then three one-line items (memory guard at EL2, real keys, flood alarm and more throughput), and one line "Today: the monitor detects, but does not block". The formula and the E2c limit moved to the speaker notes (E2c stays on slide 9). Type sizes raised so the slide still reads full.

## 13. Update 2026-11-25
- Slide 11: the words "promise 3" were removed from the first bullet; it now reads "Secure log in flash (designed, not built)".

## 14. Update 2026-11-25 (slide 9 redesigned; future work slide kept as edited by the user)
- Slide 9 (results): rebuilt with less content. A grid of 32 squares (31 blue, 1 orange = E2b), a short "Why not 32 of 32?" list (E2b failed: forwarder not started at boot; fixed with a new image, passes now; new limit after the fix: E2c), three big numbers (0 lost up to 640 frames/s with every drop counted; 18 of 18 load runs exact, 278,204 frames accounted; 12 limits documented) and one line of trust proofs checked on the board. The per-group bars (A 4/4, B 3/3, C 10/10, D 12/12, E 2/3), the loss bars and the formula are in the speaker notes.
- Slide 11 (future work): the user edited it by hand in the editor (removed "(designed, not built)", blue lead words). It was NOT changed by this update; the saved copy in `final_deck_20261125/` now matches the user's version.

## 15. Update 2026-11-25 (slide 9 third design: map of all 32 tests)
- Slide 9 now shows ALL 32 tests as a map, grouped (Boot and setup 4, Normal traffic 3, Attacks 10, Load and loss 12, Restart and reboot 3), each tile labelled with its test id. Title "All 32 tests pass". Legend: blue = 31 passed the first time, orange = 1 passed after a fix (E2b). Three numbers: 0 lost up to 640 frames/s, 18 of 18 load runs exact, 12 limits documented. All other detail is in the speaker notes.
- Wording check: "all 32 pass" is true as a statement that every one of the 32 cases now has a passing result (31 first time, E2b after the fix); the 31 ran on the earlier image and E2b on the new service image. The notes say so; if the professor wants the strict wording use "31 of 32 on the first run, E2b fixed and re-tested" (slide 3 says that).
- Previous versions are stored in `slide_backups/`: `results_v1_bars_20261125.html` (bars) and `results_v2_grid_31of32_20261125.html` (31 of 32 grid). To restore one: copy it over `project/slides/results.html` and publish.

## 16. Update 2026-11-25 (slide 9 fourth design: ring of 32 and five claims)
- Slide 9: a ring of 32 segments (31 blue, 1 orange = E2b, at the top) with "32/32 tests pass" in the middle, and five plain-language claims with counts: 4/4 starts and runs on its own; 3/3 accepts normal traffic; 10/10 catches every attack we sent; 12/12 never loses a frame silently; 3/3 survives restarts and reboots. Legend: 31 passed the first time; 1 passed after a fix (E2b, forwarder start at boot). Reason for the change: test ids (C5b, D5f) mean nothing to the audience; the claims say what was proven.
- Backups of all earlier versions of slide 9 are in `slide_backups/`: v1 bars, v2 grid 31 of 32, v3 map of 32 test ids.

## 17. Update 2026-11-25 (slide 7 diagram wider and taller)
- Slide 7 (attacks): the wall diagram was rebuilt wider and taller: lines 12 px thick (were 8), wider walls (36 px), rows 50 px (were 42), the diagram uses the full 1664 px width (lines start at x 400, walls at 620, 840, 1060, 1280, verdict column at 1380), larger labels (28 px) and verdicts (26 px). Content ends at about y 875. The previous narrow version is saved as `slide_backups/attacks_v1_narrow_20261125.html`.

## 18. Update 2026-11-25 (slide 7 less text)
- Slide 7: label above the title shortened to "WORK DONE · CHECKS AND ATTACKS"; the "Measured" line now has two values (heartbeat 321 on time, 0 late; 6,361 requests on time). The "8 late answers counted by both sides" is in the diagram (LATE 8 counted) and in the notes.

## 19. Update 2026-11-25 (slide 3, state of the art, new content)
- Slide 3 left column now shows NAMED related work taken from `Secure_CAN_Detection_Only_Design.md` section 2: AUTOSAR SecOC (counter-based replay protection for CAN), TEECheck (RTNS 2020, a TEE vets outgoing CAN frames: prevention), iCORE (SAC 2019, a core diverted into the Secure World at boot, as our core 1), EmLog (2017, tamper-resistant logging on OP-TEE), and the stated gap: "We found no work that pairs OP-TEE with CAN attack detection when Linux is compromised." The three generic approaches (checks in Linux, external HSM, TrustZone) were removed. Right column (the five midterm promises with status) unchanged.
- TO CHECK BEFORE PRESENTING: the citations come from the design note, not from re-reading the papers; confirm titles, authors and years (SecOC has no year on the slide; Schneier and Kelsey 1998 is in the notes only). The gap statement is a "we found no" claim from the user's own literature search.
- Previous version: `slide_backups/sota_v1_generic_20261125.html`.

## 20. Update 2026-11-25 (slide 3 is the state of the art ONLY)
- Slide 3 now contains only the state of the art, as three columns: work, what it does, what we take from it (SecOC: same counter idea; TEECheck 2020: we detect, we do not prevent; iCORE 2019: our core 1 uses it; EmLog 2017: model for our secure log), plus the gap box ("We found no work that pairs OP-TEE with CAN attack detection when Linux is compromised"). The five midterm promises were REMOVED from this slide at the user's request; they are listed in the speaker notes of slide 3 and the version with them is saved as `slide_backups/sota_v2_related_work_plus_promises_20261125.html`.
- OPEN: the course structure asks for "what you promised to have in the end sem" next to the state of the art. The promises now appear nowhere on screen (slide 9 shows the test results, slide 11 the future work). Decide where they should go (for example a short slide after slide 3, or the left of slide 5).
- The deck.json section description for slide 3 was changed to "State of the art and the gap."

## 21. Update 2026-11-25 (slide 2, motivation told as a story)
- Slide 2: a picture of a CAN bus with Brakes, Engine and Steering nodes and a red Attacker node ("sends a forged frame", "CAN bus: no proof of who sent a frame"), three numbered problems (1 Anyone can send a frame; 2 The check runs in Linux; 3 The record can be rewritten, future work) and the goal ("detect these attacks in the Secure World, where Linux cannot hide them"). The four attack types (spoofed ID, tampered data, replay, silence) are in problem 1 and in the notes. Previous version (four attack cards and three trust questions): `slide_backups/problem_v1_cards_20261125.html`.

## 22. 2026-11-25 (merge of slides 2 and 3 NOT done)
- The user asked to put the motivation and the state of the art on one slide. Before publishing, the live artifact (version cae3) turned out to contain a DIFFERENT slide 3 and a changed stylesheet, published by someone else: a "state of the art and our promise" slide with "what exists, how we go further", EmLog WISTP 2018, the five promises, and a gap box, using new `.sb` styles. The user then chose: keep THEIR slide 3 as it is; do not merge.
- Nothing was published. The live deck is unchanged (11 or 12 slides as live: 12). Saved for reference: `slide_backups/sota_v4_LIVE_edited_by_someone_else_20261125.html`, `slide_backups/deck_css_LIVE_version_cae3_20261125.css`, and the unpublished merged slide in `pending_merge_20261125/` (it needs its own `.ms` style rules from `deck_css_mine_with_ms_rules.css`).
- `final_deck_20261125/` now mirrors the live deck (version cae3).
