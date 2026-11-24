# DEMO FOR THE PROFESSOR: ONE FILE, EVERYTHING INSIDE

Read part 0 first (2 minutes). Then follow the numbers. Every command in a grey box is copy and paste. Type or paste **one box at a time**.
Everything here was rehearsed on the real hardware. The numbers shown are what you will see after a **clean start** (both Pis rebooted).

## 0. Read this first

**You have 3 windows.** Always look at the text at the start of the line to know which one you are in:

| Window name | The line starts with | Its only job |
|---|---|---|
| **LAPTOP** | `aslam@Aslam-ideapad` | prepare things, wait for the Pis |
| **RECEIVER** | `#` | **LOOK**: type `alarms` and read the result |
| **SENDER** | `sender@sender` | **SEND**: send a message file |

**The whole demo is just two moves, repeated:** (1) in SENDER, send one file. (2) in RECEIVER, type `alarms` and read what changed.

**Four rules:**
1. Send each file **only once**, in the given order (1, 2, 3 ...).
2. Never type `exit` in the RECEIVER window. If you do, `ssh` again and paste the helper block (part 8) again.
3. Look only at what **changes** ("+1"), not at the big numbers.
4. If anything goes wrong, use **Plan B** (the next box). It runs the whole demo by itself.

**PLAN B: the whole demo with ONE command** (LAPTOP window). It sends everything, shows every answer, and explains in words on the screen. Press Enter to go to the next step:
```
cd ~/Minor_Project/Part_E_Verification
python3 scripts/demo_for_professor.py
```

## 1. The setup (do it 10 minutes BEFORE the professor comes)

**Step 1. Open the three windows.** Open 3 terminals. The first one is the LAPTOP window; type in it:
```
cd ~/Minor_Project/Part_E_Verification
```
Open the second terminal and type: `ssh root@10.237.27.212` (this is the RECEIVER, the line will start with `#`).
Open the third terminal and type: `ssh sender@10.237.27.144` (this is the SENDER, the line will start with `sender@sender`).

**Step 2. Reboot both Pis** (clean start, all counters at zero). In the RECEIVER window type `reboot`. In the SENDER window type `sudo reboot`. Both windows close. That is correct.

**Step 3. Wait until both are back** (LAPTOP window). It prints two "is up" lines after about 1 minute:
```
for h in root@10.237.27.212 sender@10.237.27.144; do until ssh -o BatchMode=yes -o ConnectTimeout=3 $h true 2>/dev/null; do sleep 3; done; echo "$h is up"; done
```

**Step 4. Open the RECEIVER and the SENDER again** (use the two terminals that closed): in one type `ssh root@10.237.27.212`, in the other `ssh sender@10.237.27.144`.

**Step 5. Show the receiver started everything by itself** (RECEIVER window). Do this BEFORE step 6:
```
cat /proc/uptime
ps | grep can_forwarder
/etc/init.d/S95can_forwarder status
```
You see: a small uptime (under about 2 minutes), a line `/usr/bin/can_forwarder ... --pull ...`, and `can_forwarder supervisor running`.

**Step 6. Prepare the files** (LAPTOP window). Wait for the word `READY`:
```
python3 scripts/demo_prepare.py
```

**Step 7. Teach the receiver the short commands** (RECEIVER window). Paste the **helper block from part 8** (5 lines). Nothing prints. Then type `alarms`. You see only one line, `verdict totals frame 0 ...` and **no alarm lines**. Everything is zero. Good.

**Step 8. Go to the right folder** (SENDER window):
```
cd /tmp/demo
ls
```
You see `1_normal.txt` up to `9_flood12000.txt`.

**Optional: record everything for later** (LAPTOP). Before step 1, in each terminal, you can type `script -a ~/demo_<name>.log` (for example `script -a ~/demo_receiver.log`) and later `exit` to stop. The file keeps every line you typed and every answer you saw.

Now you are ready. Wait for the professor.

## 2. What to say at the start (30 seconds, read it out)

"A CAN message cannot prove who sent it. In a car, any computer on the bus can send any message. Normally Linux checks the message, but Linux can be hacked. My monitor puts the judge inside the processor, in the Secure World, where Linux cannot reach. On this receiver, Linux is only a postman. The Secure World is the judge. I will prove that, and then I will attack it live."

## 3. Prove that the judge is the Secure World (RECEIVER window, 3 minutes)

Type one line, read the answer, say the sentence.

| Type this | You see | Say |
|---|---|---|
| `cat /sys/devices/system/cpu/online` | `0,2-3` | "Linux has only three cores. Core 1 belongs to the Secure World." |
| `dmesg \| grep -i "optee: revision"` | `optee: revision 4.10 ...` | "OP-TEE, the Secure World operating system, is running." |
| `ls -l /dev/tee0` | one line with `/dev/tee0` | "This is the only door from Linux to the Secure World." |
| `grep -i -E "10000000\|08000000" /proc/iomem` | two lines with `reserved` | "The Secure World memory is reserved. Linux may not use it." |
| `ps \| grep can_forwarder` | the forwarder line | "This is the postman. It started by itself at boot." |
| `/tmp/mon_client --retries 20 status 0 \| grep poll_count` (type it twice, 2 seconds apart) | the number grows by tens of millions | "Core 1 runs its own loop by itself, without Linux." |

**Which core does what: one table (LAPTOP window).** It reads the receiver and prints one table: each of the 4 cores, who owns it, what runs on it, and the proof:
```
python3 scripts/demo_cores.py
```
You see cores 0, 2 and 3 as `Linux` (with the postman `can_forwarder` and the normal programs) and core 1 as `SECURE WORLD`, with the line "its own loop counter grew by 38,xxx,xxx in 1 s". Say: "Linux was refused core 1 at boot (the boot log line). Core 1 belongs to the Secure World. The growing number is read from the Secure World itself, so Linux cannot fake it." If you send a file and run it again, the "CAN-chip interrupts" number on a Linux core goes up.

Then in the **LAPTOP** window:
```
python3 scripts/demo_prove_no_key.py
```
You see `keys inside: NONE` and `RESULT: the program that carries the messages holds no key`. Say: "The postman has no key. So it cannot judge a message and it cannot forge a verdict. The keys exist only in the Secure World."

## 4. Normal traffic and the attacks (the main part, 10 minutes)

Use the same two moves for every row: **SEND** in the SENDER window, then **LOOK** in the RECEIVER window (type `alarms`, and `evt` to see the time).
Numbers below are for a clean start. TIMEOUT usually goes +1 after each file. It only means the bus went quiet. Ignore it.

| # | SEND (SENDER window) | LOOK: you see (RECEIVER) | Say |
|---|---|---|---|
| 1 | `python3 laptop_can_send.py --iface can0 1_normal.txt` | `frame 10`, no alarm | "10 correct messages. All accepted. No alarm." |
| 2 | `python3 laptop_can_send.py --iface can0 2_tamper.txt` | `frame 16`, **`AUTH_FAIL 1`** | "One message had a data bit changed but the old signature. Only the Secure World has the key, so only it could see that." |
| 3 | `python3 laptop_can_send.py --iface can0 3_replay.txt` | `frame 21`, **`REPLAY 1`** (`evt`: detail 1) | "An old message sent again. Correct signature, but the counter is old." |
| 4 | `python3 laptop_can_send.py --iface can0 4_duplicate.txt` | `frame 24`, **`REPLAY 2`** (`evt`: detail 2) | "The last message sent a second time." |
| 5 | `python3 laptop_can_send.py --iface can0 5_conflict.txt` | `frame 27`, **`REPLAY 3`** (`evt`: detail 3) | "Same counter but different data. Even a signed message is refused if its counter is used." |
| 6 | `python3 laptop_can_send.py --iface can0 6_wrongid.txt` | `frame 31`, **`MALFORMED 1`** | "A CAN ID that is not allowed on this link." |
| 7 | `python3 laptop_can_send.py --iface can0 7_jump.txt` | `frame 34`, **`COUNTER_GAP 1`** (`evt`: detail 4) | "The counter skipped 4 numbers. Messages were removed or injected. It noticed." |

**Why the alarm is only 1 when the file has 5 messages:** each file has good messages around one bad message. Example: `3_replay.txt` has 4 good messages and 1 old message. The Secure World accepts the 4 and catches the 1.

**The first file after a reboot** also makes one `COUNTER_RESYNC` (the Secure World learns the starting counter). It is not in `alarms`. To show it, type in the RECEIVER window: `/tmp/mon_client --retries 20 status 1 | grep RESYNC`. Say: "That is the start-up. It is by design."

## 4b. THE LIVE STREAM (the professor's request): packets that never stop, coloured

**THE EASY WAY: ONE command opens both windows at the same time** (LAPTOP window, in the project folder):
```
bash scripts/demo_live_stream.sh
```
Two big windows open: **SENDER on top** (a new packet every 0.1 s) and **RECEIVER below** (every packet with its colour, and the Secure World totals in the bottom bar). The receiver starts first, the sender 3 seconds later, and both run forever. Press the attack keys in the **SENDER (top) window**. **The sender now attacks by itself:** one attack every 2 to 3 seconds (change: python3 stream.py --every 5-8), all six types in turn, so every colour appears within about 15 seconds without touching a key (you can still press `t r d c w j` yourself; click the TOP window first so the key goes there; press `a` to switch the automatic attacks off). If you want only green valid packets until you press a key: `bash scripts/demo_live_stream.sh --quiet`. Stop: `q` in the sender window, Ctrl+C in the receiver window. Never start two streams at once (two streams with the same counters look like replays); the command stops any leftover one for you.
The three steps below are the same thing done by hand.

One command on the sender sends a **new** packet again and again (counter +1, speed and rpm change in every packet). The receiver shows every packet and its colour. **Repeats are never continuous:** you press a key, and attacks are at least 1.5 seconds apart.

| # | Window | Type | You see |
|---|---|---|---|
| 1 | LAPTOP | `python3 scripts/demo_stream_setup.py` | `READY ... the stream will start at N` (run it again after any reboot, and after you used the numbered files) |
| 2 | RECEIVER | `/tmp/packet_view` | a header with the colour legend, then packets appear one per line; the last line is a status bar with the Secure World totals |
| 3 | SENDER | `cd /tmp/demo` then `python3 stream.py` | a new line every 0.1 s: `ctr 43 speed 98.3 km/h rpm 1450 ...` |

**Colours (shown on the RECEIVER; the sender window stays green on purpose, add --label to see its own colours):**

| Colour | Packet | Key in the sender window |
|---|---|---|
| **green** | valid, accepted | (normal packets) |
| **red** | tampered | `t` |
| **orange** | replay (old packet again) | `r` |
| **light blue** | duplicate (last packet again) | `d` |
| **purple** | conflict (same counter, other data) | `c` |
| **pink** | wrong id | `w` |
| **yellow** | counter gap | `j` |
| grey | bus events (silence, traffic back) | |

**Keys in the SENDER window** (press the key, no Enter): `t` tamper, `r` replay, `d` duplicate, `c` conflict, `w` wrong id, `j` counter jump, `a` automatic attacks every 6 to 10 s on/off, `p` pause, `+` `-` speed, `q` quit.
After a key press the sender line is red and says what it did; about 0.7 s later the **same packet** turns red on the receiver with the Secure World's reason, for example `✖ TAMPERED: the signature does not fit the data (AUTH_FAIL)`.

**Say:** "Every packet is different. The sender never repeats a packet by itself. I press a key to attack. The receiver shows each packet and the colour is the Secure World's decision."
**Stop:** `q` in the sender window, Ctrl+C in the receiver window. To remove the demo key from the sender: `python3 scripts/demo_stream_setup.py --remove-key` (LAPTOP).
Honest points: the sender plays the car's trusted ECU, so its RAM holds the key for this demo (the receiver's postman never has it). The colour comes from the Secure World's event log (one event per second per attack type), which is why attacks are spaced 1.5 s apart. Details: `tools/live_demo/README.md`.

## 5. The time and the timeout (RECEIVER window, 2 minutes, send nothing)

| Type this | You see | Say |
|---|---|---|
| `age` | `last good message arrived 2.0 s ago` | "The Secure World measures time with its own clock." |
| `sleep 3; age` | `... 5.0 s ago` | "The number grows while the bus is silent." |
| `evt` | `TIMEOUT detail 1 ... s ago` | "After 0.3 seconds with no valid message, the Secure World raised a TIMEOUT by itself." |

## 6. Many messages at once (3 minutes)

| # | Window | Type | You see | Say |
|---|---|---|---|---|
| 1 | RECEIVER | `A=$(snap)` | nothing | "I remember the counters before." |
| 2 | SENDER | `python3 burst_send.py 8_burst2000.txt retry` | a line with `"sent_ok": 2000` | "2000 messages as fast as the bus allows." |
| 3 | RECEIVER (after 5 s) | `gapcheck` | `counter span 2000 \| judged 2000 \| missing 0` | "All 2000 judged. None lost." |
| 4 | RECEIVER | `A=$(snap)` | nothing | "Now the flood." |
| 5 | SENDER | `python3 burst_send.py 9_flood12000.txt retry` | `"sent_ok": 12000` | "12000 messages. The Secure World can judge only 640 per second." |
| 6 | RECEIVER (after about 12 s, when it stops changing) | `gapcheck` | `counter span 12000 \| judged about 4200 \| missing about 7800` | "Judged plus missing equals 12000. The Secure World counted exactly what was dropped. Nothing disappeared without being counted." |

## 7. What if Linux is killed? (RECEIVER window, 2 minutes)

| # | Type this | You see | Say |
|---|---|---|---|
| 1 | `/etc/init.d/S95can_forwarder stop >/dev/null 2>&1 </dev/null` | nothing | "I stop the postman." |
| 2 | wait 6 seconds, then `evt` | `TIMEOUT detail 4` and `TIMEOUT detail 2` | "The Secure World asked Linux every second and Linux did not answer. After 3 misses it raised the alarm." |
| 3 | `/tmp/mon_client --retries 20 status 0 \| grep -o "hb_state [A-Z_]* hb_miss_count [0-9]*"` | `MISSED ... 6` | "It counts the misses by itself." |
| 4 | `/etc/init.d/S95can_forwarder start >/dev/null 2>&1 </dev/null`, wait 4 seconds, then repeat line 3 | `ANSWERED ... 0` | "The postman is back and answers again." |

**Keep the `>/dev/null 2>&1 </dev/null` part exactly as written.** Without it, the command never finishes.

## 8. The helper block (paste ONCE in the RECEIVER window, after every reboot)

Copy all five lines together and paste them. Nothing prints. This creates the short commands `alarms`, `evt`, `age`, `snap`, `gapcheck`.
```sh
evt()  { now=$(/tmp/mon_client --retries 20 status 0 | grep -o "now_ticks [0-9]*" | cut -d" " -f2); n=$(/tmp/mon_client --retries 20 status 0 | grep -o "ev_seq [0-9]*" | cut -d" " -f2); f=$((n>7?n-7:1)); /tmp/mon_client --retries 20 events --from $f --all | awk -v now=$now '/^seq/ {printf "event %s  %-18s detail %-3s  %.1f s ago\n", $2, $3, $7, (now-$9)/19200000}'; }
age()  { /tmp/mon_client --retries 20 status 0 | grep -o "traffic_age_ticks [0-9]*" | awk '{printf "last good message arrived %.2f s ago\n", $2/19200000}'; }
alarms() { /tmp/mon_client --retries 20 status 1 | grep -E "AUTH_FAIL|REPLAY|COUNTER_GAP|MALFORMED|TIMEOUT|verdict totals"; }
snap() { echo "$(/tmp/mon_client --retries 20 status 0 | grep -o "last_counter [0-9]*" | cut -d" " -f2) $(/tmp/mon_client --retries 20 status 1 | grep -o "frame [0-9]*" | head -1 | cut -d" " -f2)"; }
gapcheck() { set -- $A $(snap); echo "counter span $(( $3 - $1 )) | judged $(( $4 - $2 )) | missing (dropped but counted) $(( $3 - $1 - $4 + $2 ))"; }
```

## 9. How to finish (30 seconds, read it out)

"I tested this in 31 checks. Up to 640 messages per second nothing is lost, and above that every dropped message is counted. The Secure World's own counter agrees with the number dropped. The full results are in `FINAL_TEST_REPORT.md`. The limits: it detects but it does not block. The keys are prototype keys. The Pi 3 has no memory-protection hardware, so the isolation relies on the memory reservation. A secure log is future work."

## 10. If something goes wrong (do not panic)

| What you see | What it means | What to do |
|---|---|---|
| `alarms: not found` | the helper block is not in this window | paste the helper block (part 8) again |
| `No such file ... mon_client` | the Pi was rebooted and the tools are gone | LAPTOP: `python3 scripts/demo_prepare.py`, then paste the helper block again |
| `python3: can't open file ...` | you are not in the right folder on the sender | SENDER: `cd /tmp/demo` |
| an alarm moved twice | a file was sent twice | stay calm and say "the Secure World also catches my repeat as a replay"; or reboot both and start again from part 1 |
| the number is not what is written here | normal if the Pi was not freshly rebooted | look only at the change (+1) |
| the RECEIVER window closed | you typed `exit` or the connection dropped | `ssh root@10.237.27.212`, then paste the helper block again |
| the Pi does not answer | still booting, or the cable | wait 1 minute; check cables; the receiver MAC is `b8:27:eb:5c:ba:a9` |
| anything else | | **PLAN B** (part 0): `python3 scripts/demo_for_professor.py` |

## 11. Questions the professor may ask (short answers)

| Question | Answer |
|---|---|
| Why the Secure World and not Linux? | If Linux is hacked, the attacker controls Linux but not the Secure World. The keys and the verdict live only there. |
| Where is the key? | Only in the Secure World image. The postman program has none (proved in part 3). The keys are prototype keys written in the source. |
| Can Linux fake an answer? | It can only send messages and heartbeat answers through `/dev/tee0`. It cannot write the Secure World's counters. If it stops answering, the Secure World raises TIMEOUT (part 7). |
| What is HMAC? | A signature made with a secret key. Change one bit of the data and it no longer matches. |
| What is a replay? | An old, correctly signed message sent again. The counter check refuses it. |
| What did you measure? | 31 PASS in the final campaign. No loss up to 640 messages per second. Above that, every dropped message is counted and equals the Secure World's own gap. |
| What was the big problem you solved? | The Secure World was 45 to 100 times too slow because of one debug build option (`CFG_CORE_DEBUG_CHECK_STACKS=y`). Turning it off fixed it. |
| What are the limits? | It detects but does not block. Prototype keys. No memory-protection hardware (TZASC) on the Pi 3. No secure log yet. |
| Why 640 per second? | The Secure World asks for up to 32 messages every 50 ms. This is the design limit. The CPU is no longer the limit. |

## 12. Where the proof is (if the professor wants to read)

* `FINAL_TEST_REPORT.md`: all 56 rows of results and the limits.
* `CHECKLIST_FROM_REPORT.md`: every row with how to check it. One command with no hardware re-checks all evidence: `python3 scripts/check_report_evidence.py`.
* `REAL_BUS_RESULTS.md`: the measurements and the story (sections 10.9 to 10.11).
