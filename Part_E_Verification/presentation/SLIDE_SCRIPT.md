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
