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
