# Live demo guide: what to run, why, and how it proves the work is done on the receiver

Everything below was rehearsed on the real hardware on 2026-11-23 (log: `baseline/demo_rehearsal_20261123.txt`). The outputs shown are the real ones.

## 0. The idea in one minute (say this first)

```
 SENDER Pi  ----- real CAN-FD bus ----->  RECEIVER Pi
 (the car and the attacker)               CAN chip -> Linux "forwarder" (only a postman)
                                                          |  hands every message over
                                                          v
                                          SECURE WORLD (OP-TEE, CPU core 1): the judge
                                          checks the secret signature (HMAC), the counter, the format, the timing
```

* The **sender** only sends messages (good ones and bad ones). It never judges anything.
* The **receiver** judges. Linux on the receiver is only a postman. The judge is the **Secure World** of the processor: Linux cannot read its memory, cannot read its keys, and cannot change its answer.
* To prove "the work is done on the receiver, in the Secure World" you show: (1) Linux does not own that core and that memory, (2) the program Linux runs has no key, (3) the answers come from counters that only the Secure World keeps, and they change exactly when you send something.

## 1. Setup before the professor arrives (5 minutes)

| Window | Where | Command | Why |
|---|---|---|---|
| **L** (laptop) | laptop | `cd ~/Minor_Project/Part_E_Verification` | work folder |
| **L** | laptop | `python3 scripts/demo_prepare.py` | Reads the Secure World's message counter and makes 9 packet files whose counters continue from it, then copies them to the sender. Run it again whenever you want to start the demo from the beginning. |
| **R** (receiver) | new terminal | `ssh root@10.237.27.212` | the judge; you watch it |
| **S** (sender) | new terminal | `ssh sender@10.237.27.144` then `cd /tmp/demo` | the attacker; you send from here |

Paste this once into window **R** (two small helpers):
```sh
ev()   { n=$(/tmp/mon_client --retries 20 status 0 | grep -o "ev_seq [0-9]*" | cut -d" " -f2); f=$((n>5?n-5:1)); /tmp/mon_client --retries 20 events --from $f --all; }
live() { while true; do clear; /tmp/mon_client --retries 20 status 0 | grep -E "last_counter|hb_state"; /tmp/mon_client --retries 20 status 1 | head -16; sleep 1; done; }
```
`ev` shows the last events of the Secure World (what it decided). `live` is a screen that refreshes every second (stop it with Ctrl+C).
If the addresses changed (DHCP), find the receiver by MAC `b8:27:eb:5c:ba:a9` and the sender by `b8:27:eb:1e:98:02`, and run `RX=<ip> SN=<ip> python3 scripts/demo_prepare.py`.

## 2. PART 1: prove that the judge is the Secure World (window R, about 4 minutes)

| # | Command (on the receiver) | What you will see | What it proves, in simple words |
|---|---|---|---|
| 1 | `cat /sys/devices/system/cpu/online` | `0,2-3` | Linux has only 3 cores (0, 2, 3). **Core 1 is not Linux's.** It is reserved for the Secure World. |
| 2 | `dmesg \| grep -i "optee: revision"` and `ls -l /dev/tee0` | `optee: revision 4.10 ...` and the device `/dev/tee0` | OP-TEE (the Secure World operating system) is running. `/dev/tee0` is the **only door** from Linux to the Secure World. |
| 3 | `grep -i -E "10000000\|08000000" /proc/iomem` | `08000000-09ffffff : reserved` and `10000000-10ffffff : reserved` | The Secure World's memory is **reserved**: Linux is told not to use it. (Pi 3 has no memory-protection hardware, so this reservation is what keeps Linux out; honest limit.) |
| 4 | `ps \| grep can_forwarder` | `/usr/bin/can_forwarder --iface can0 run ... --pull ...` | This is the **postman**: the Linux program that carries messages. It started by itself at boot. |
| 5 (laptop **L**) | `python3 scripts/demo_prove_no_key.py` | `keys inside: NONE` for the forwarder and the tools; boot partition mounted in Linux: NO | The postman has **no key**, so it cannot judge or forge anything. The keys exist only in the Secure World image, which Linux cannot even see (the boot partition is not mounted). |
| 6 | `/tmp/mon_client --retries 20 status 0` (run twice, 2 s apart) | `core1_poll_age_ticks 5114 poll_count 36393019120`, then `... 36469693230` | **Core 1 is alive by itself.** `poll_count` is Core 1's own loop counter: it grows by tens of millions every second without Linux doing anything. `poll_age` is how long ago Core 1 last looked (tiny, a fraction of a millisecond). |
| 7 | `/tmp/mon_client --retries 20 status 2` | `answers ON_TIME 18936 LATE 1 ... max 49.100 ms` and a histogram | The Secure World measures **with its own clock** how fast Linux answers its requests. Linux cannot hide a late answer. |
| 8 | `/tmp/canmode can0 \| grep -E "nominal\|data bit\|state"` | `nominal 500000`, `data 2000000`, `ERROR-ACTIVE`, `<FD>` | The CAN-FD link is real: 500 kbit/s and 2 Mbit/s. |

Optional "everything is healthy" proof from the laptop (17 checks in one go):
`cd tools/verify_boot && ./verify_boot.sh 10.237.27.212 --expect-core1-pinned` -> `verify_boot: 17 pass, 0 FAIL`.

## 3. PART 2: attacks, one by one (window S sends, window R shows)

On **S** (in `/tmp/demo`) send the files **in this order**. After each one, on **R** type `ev`.
The first file after a reboot shows one `COUNTER_RESYNC`: the monitor has no earlier counter, so it synchronises once. That is normal start-up, not an attack.

| Step | Sender command (window S) | What the attacker does | Secure World answer (window R, real output) | Proves |
|---|---|---|---|---|
| 1 | `python3 laptop_can_send.py --iface can0 1_normal.txt` | 10 correct messages | `judged +10`, no alarm (the first time after reboot: `COUNTER_RESYNC`) | correct traffic is accepted |
| 2 | `python3 laptop_can_send.py --iface can0 2_tamper.txt` | changes one data bit, keeps the old signature | `AUTH_FAIL` | only someone who knows the key can see the signature is wrong, and Linux has no key |
| 3 | `python3 laptop_can_send.py --iface can0 3_replay.txt` | sends an old valid message again | `REPLAY` detail 1 | the counter check |
| 4 | `python3 laptop_can_send.py --iface can0 4_duplicate.txt` | sends the last message again, byte for byte | `REPLAY` detail 2 | duplicates are caught |
| 5 | `python3 laptop_can_send.py --iface can0 5_conflict.txt` | same counter, different data, valid signature | `REPLAY` detail 3 | even a correctly signed message is refused if its counter is used |
| 6 | `python3 laptop_can_send.py --iface can0 6_wrongid.txt` | uses a CAN id that is not allowed (0x124) | `MALFORMED` detail 3 | format check; very cheap to refuse |
| 7 | `python3 laptop_can_send.py --iface can0 7_jump.txt` | skips 4 counters (messages removed or injected) | `COUNTER_GAP` detail 4 | missing messages are noticed |

The numbers from the rehearsal: `judged +10 / +6 / +5 / +3 / +3 / +4 / +3`, and each alarm appeared exactly once.

**How to say "it is done on the receiver":** the sender window only prints that it sent frames. The verdict (`AUTH_FAIL`, `REPLAY`, ...) appears **only on the receiver**, read from the Secure World's own counters through `/dev/tee0`. Show both windows side by side.

## 4. PART 3: speed and overload (window S sends, window R shows)

| Step | Sender command | What it shows | Real numbers from the rehearsal |
|---|---|---|---|
| 8 | `python3 burst_send.py 8_burst2000.txt retry` | 2000 messages as fast as the bus allows | sender: `rate_fps 3752`; receiver: judged **+2000 of 2000**, gap events 0 (nothing lost) |
| 9 | `python3 burst_send.py 9_flood12000.txt retry` | 12000 messages, more than the monitor can judge (it judges 640 per second) | judged **4203**, so **7797 dropped**; the Secure World's own counter gap says **7797 missing**: the two numbers agree, nothing disappeared without being counted |

On **R** after step 9: `/tmp/mon_client --retries 20 status 1` shows the `COUNTER_GAP` count; `ev` shows the gap events (`detail` = number of missing messages in that gap).
Why messages are dropped: the Linux postman holds 2048 messages and, when full, throws away the **oldest**. The Secure World sees the hole in the counters and counts it. Honest policy: nothing is hidden.

## 5. PART 4: what if Linux is killed? (window R)

```sh
/etc/init.d/S95can_forwarder stop >/dev/null 2>&1 </dev/null     # kill the postman
sleep 6; ev                                                       # shows TIMEOUT detail 4 and detail 2
/etc/init.d/S95can_forwarder start >/dev/null 2>&1 </dev/null    # postman back
```
The Secure World sends a challenge every second; Linux must answer. After 3 missed challenges the Secure World raises **TIMEOUT detail 2** (and detail 4 because the packet requests are not answered either); `status 0` then shows `hb_state MISSED hb_miss_count 6` (rehearsal). When you start the postman again, the heartbeat is `ANSWERED` after about 4 seconds. This proves the Secure World **watches Linux**, not the other way round. (The `>/dev/null 2>&1 </dev/null` part is needed when you run start/restart over ssh; otherwise the command never returns. Known limit.)

## 6. One-command version and fallbacks

* Everything above, narrated and checked automatically: `python3 scripts/demo_for_professor.py` (Enter between steps; `--auto` for timed pauses; about 5 minutes).
* Something looks wrong: run `python3 scripts/demo_prepare.py` again (new chain of files). Receiver not answering: check power and the address (MAC `b8:27:eb:5c:ba:a9`). Forwarder missing: `/etc/init.d/S95can_forwarder start >/dev/null 2>&1 </dev/null`.
* Do not run `board_full_check.sh` during the demo with the forwarder running (it plays the heartbeat responder itself).

## 7. Questions your professor may ask (short answers)

| Question | Answer |
|---|---|
| Why is the Secure World better than Linux for this? | If Linux is hacked, the attacker controls Linux but not the Secure World (TrustZone separates them in hardware). The keys and the verdict live only there. |
| Where is the key? | Inside the Secure World image only. The postman binary contains none of the three keys (proof 5), and the boot partition is not even mounted in Linux. It is a prototype key written in the source (limit: anyone with the build tree can forge). |
| Can Linux fake a verdict? | Linux can only send messages and heartbeat answers through `/dev/tee0`. It cannot write the Secure World's counters. If it stops answering, the Secure World raises TIMEOUT (part 4). |
| What is HMAC? | A signature made with a secret key. Change one bit and it no longer matches (`AUTH_FAIL`). |
| What is a replay? | An old, correctly signed message sent again. The counter check refuses it. |
| What did you measure? | 31 PASS in the final campaign (`FINAL_TEST_REPORT.md`): 0 loss up to 640 messages per second and for bursts up to about 2490 messages; above that every loss is counted and equals the Secure World's gap. |
| Why 640 per second? | Design: the Secure World asks for up to 32 messages every 50 ms. The CPU is not the limit any more (one HMAC takes 22 to 55 microseconds). |
| What was the big problem you solved? | The Secure World was 45 to 100 times too slow because of one debug build option (`CFG_CORE_DEBUG_CHECK_STACKS=y`, a check on every function call). Switching it off fixed it. |
| What are the limits? | Detection only (it does not block); prototype keys; the Pi 3 has no memory-protection hardware (TZASC), so isolation relies on the reservation; the postman drops the oldest message when overloaded; no secure audit log yet. |

## 8. Command glossary

| Command | Where | What it does |
|---|---|---|
| `scripts/demo_prepare.py` | laptop | builds the 9 chained packet files from the live counter and copies them to the sender |
| `scripts/demo_prove_no_key.py` | laptop | reads the real programs on the receiver and shows that none contains a key |
| `scripts/demo_for_professor.py` | laptop | the full narrated demo in one command |
| `laptop_can_send.py --iface can0 FILE` | sender | sends a packet file onto the CAN-FD bus at the speed written in the file (100 ms between messages) |
| `burst_send.py FILE retry` | sender | sends a file as fast as the bus allows and prints a json line (`sent_ok`, `rate_fps`) |
| `/tmp/mon_client status 0` | receiver | Secure World health: last counter, heartbeat state, Core 1 loop counters |
| `/tmp/mon_client status 1` | receiver | totals: messages judged and a counter per alarm type (exact) |
| `/tmp/mon_client status 2` | receiver | how fast the answers were, measured by the Secure World's clock |
| `/tmp/mon_client events --from N --all` | receiver | the Secure World's event log (what it decided and why) |
| `/tmp/canmode can0` | receiver | CAN-FD settings and state of the interface |
| `/etc/init.d/S95can_forwarder start\|stop\|status` | receiver | controls the postman |

## 9. From zero: show AUTH_FAIL, a counter mismatch, a timeout, and the time (rehearsed 2026-11-23)

**What a message (packet) is.** `[ ID | counter | data | signature ]`. The sender writes a **counter** that goes up by one for every message (1, 2, 3 ...) and a **signature** (HMAC) made with a secret key. The Secure World knows the key and remembers the last counter. It then decides:

| What goes wrong | The Secure World says | Why |
|---|---|---|
| the data was changed, the signature is old | **AUTH_FAIL** | the signature does not match the data (needs the key to notice) |
| the counter is **too old or repeated** (same as before) | **REPLAY** (detail 1 old, 2 duplicate, 3 same counter other data) | an old message sent again |
| the counter **jumps ahead** (1, 2, 3, then 8) | **COUNTER_GAP** (detail = how many are missing) | messages were removed or injected |
| **nothing arrives** for more than 0.3 s | **TIMEOUT** detail 1 | the bus went silent |
| Linux **stops answering** the heartbeat (3 times) | **TIMEOUT** detail 2 | the postman is dead |

**Step 1, laptop (window L):** `cd ~/Minor_Project/Part_E_Verification && python3 scripts/demo_prepare.py` (builds fresh files from the live counter).

**Step 2, receiver (window R):** `ssh root@10.237.27.212`, then paste these helpers once. `evt` shows the last events **with the time** ("x s ago"), `age` shows how long ago the last good message arrived, `alarms` shows the exact alarm counters:
```sh
evt()  { now=$(/tmp/mon_client --retries 20 status 0 | grep -o "now_ticks [0-9]*" | cut -d" " -f2); n=$(/tmp/mon_client --retries 20 status 0 | grep -o "ev_seq [0-9]*" | cut -d" " -f2); f=$((n>7?n-7:1)); /tmp/mon_client --retries 20 events --from $f --all | awk -v now=$now '/^seq/ {printf "event %s  %-18s detail %-3s  %.1f s ago\n", $2, $3, $7, (now-$9)/19200000}'; }
age()  { /tmp/mon_client --retries 20 status 0 | grep -o "traffic_age_ticks [0-9]*" | awk '{printf "last good message arrived %.2f s ago\n", $2/19200000}'; }
alarms() { /tmp/mon_client --retries 20 status 1 | grep -E "AUTH_FAIL|REPLAY|COUNTER_GAP|MALFORMED|TIMEOUT|verdict totals"; }
```
Type `alarms` now: write down the numbers (this is the "before").

**Step 3, sender (window S):** `ssh sender@10.237.27.144`, then `cd /tmp/demo`. Send the files **in order**, one at a time; after each, type `alarms` or `evt` in window R:

| Order | Sender command | Receiver shows (rehearsal) |
|---|---|---|
| 1 | `python3 laptop_can_send.py --iface can0 1_normal.txt` | 10 more messages judged, no new alarm |
| 2 | `python3 laptop_can_send.py --iface can0 2_tamper.txt` | **AUTH_FAIL 1 -> 2** (`event 37 AUTH_FAIL detail 0  5.4 s ago`) |
| 3 | `python3 laptop_can_send.py --iface can0 3_replay.txt` | **REPLAY 3 -> 4** (`event 40 REPLAY detail 1  2.4 s ago`): the counter mismatch "too old" |
| 4, 5, 6 | `... 4_duplicate.txt`, `... 5_conflict.txt`, `... 6_wrongid.txt` | REPLAY detail 2, REPLAY detail 3, MALFORMED (send these before file 7: its counters continue after them) |
| 7 | `python3 laptop_can_send.py --iface can0 7_jump.txt` | **COUNTER_GAP +1, detail 4**: the counter mismatch "jumped ahead, 4 missing" |

**Step 4, the timeout and the time (stop sending, look at window R):**
```sh
age       # "last good message arrived 2.04 s ago"
sleep 3; age   # "... 5.97 s ago"   the number grows while the bus is silent
evt       # every file ends with: event 41  TIMEOUT detail 1  1.9 s ago
```
Say: "the Secure World keeps its own clock: after 0.3 seconds without a valid message it raised TIMEOUT detail 1; when I send again it raises TRAFFIC_RECOVERED". The times come from the Secure World's own counter (19.2 million ticks per second), not from Linux.
For the other timeout (Linux dead) use part 5 of this guide: stop the forwarder, wait 6 s, type `evt` -> `TIMEOUT detail 4` and `TIMEOUT detail 2`.
