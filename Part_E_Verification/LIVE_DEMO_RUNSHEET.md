# Live demo run sheet: every line, in order (about 12 minutes)

Windows: **L** = laptop, **R** = receiver (`ssh root@10.237.27.212`), **S** = sender (`ssh sender@10.237.27.144`). Every command below was rehearsed on the real hardware.
Send the sender files **once each, in order**. The numbers you see on screen will be different from the examples; what counts is the **change** ("+1").

## PART 0. Setup (before the audience)

| # | Window | Type | You see | Why |
|---|---|---|---|---|
| 0.1 | L | `cd ~/Minor_Project/Part_E_Verification` then `python3 scripts/demo_prepare.py` | `READY ... 9 files are on the sender` | builds the files from the live counter |
| 0.2 | R | `ssh root@10.237.27.212` | prompt `#` | open the receiver |
| 0.3 | R | `cat /sys/class/net/eth0/address` | `b8:27:eb:5c:ba:a9` | right Pi |
| 0.4 | R | paste the 5 helpers (block A below) | nothing | short commands |
| 0.5 | S | `ssh sender@10.237.27.144` then `cd /tmp/demo` then `ls` | `1_normal.txt ... 9_flood12000.txt` | the folder with the files |

**Block A (paste once in R):**
```sh
evt()  { now=$(/tmp/mon_client --retries 20 status 0 | grep -o "now_ticks [0-9]*" | cut -d" " -f2); n=$(/tmp/mon_client --retries 20 status 0 | grep -o "ev_seq [0-9]*" | cut -d" " -f2); f=$((n>7?n-7:1)); /tmp/mon_client --retries 20 events --from $f --all | awk -v now=$now '/^seq/ {printf "event %s  %-18s detail %-3s  %.1f s ago\n", $2, $3, $7, (now-$9)/19200000}'; }
age()  { /tmp/mon_client --retries 20 status 0 | grep -o "traffic_age_ticks [0-9]*" | awk '{printf "last good message arrived %.2f s ago\n", $2/19200000}'; }
alarms() { /tmp/mon_client --retries 20 status 1 | grep -E "AUTH_FAIL|REPLAY|COUNTER_GAP|MALFORMED|TIMEOUT|verdict totals"; }
snap() { echo "$(/tmp/mon_client --retries 20 status 0 | grep -o "last_counter [0-9]*" | cut -d" " -f2) $(/tmp/mon_client --retries 20 status 1 | grep -o "frame [0-9]*" | head -1 | cut -d" " -f2)"; }
gapcheck() { set -- $A $(snap); echo "counter span $(( $3 - $1 )) | judged $(( $4 - $2 )) | missing (dropped but counted) $(( $3 - $1 - $4 + $2 ))"; }
```

## PART 1. Say the idea (30 seconds)
"A CAN message does not prove who sent it. Today Linux checks it, and Linux can be hacked. My monitor puts the judge in the **Secure World** of the processor. Linux on the receiver is only a postman. I will prove that, then attack it live."

## PART 2. Prove it is the Secure World (window R, 3 minutes)

| # | Type | You see | Say |
|---|---|---|---|
| 2.1 | `cat /sys/devices/system/cpu/online` | `0,2-3` | "Linux has only 3 cores. Core 1 belongs to the Secure World." |
| 2.2 | `dmesg \| grep -i "optee: revision"` | `optee: revision 4.10 ...` | "OP-TEE, the Secure World OS, is running." |
| 2.3 | `ls -l /dev/tee0` | `crw-rw---- ... /dev/tee0` | "This is the only door from Linux to the Secure World." |
| 2.4 | `grep -i -E "10000000\|08000000" /proc/iomem` | two lines `reserved` | "The Secure World memory is reserved: Linux may not use it." |
| 2.5 | `ps \| grep can_forwarder` | `/usr/bin/can_forwarder ... --pull ...` | "This is the postman. It started by itself at boot." |
| 2.6 | (window L) `python3 scripts/demo_prove_no_key.py` | `keys inside: NONE` ... `RESULT: the program that carries the messages holds no key` | "The postman has no key, so it cannot judge or forge." |
| 2.7 | `/tmp/mon_client --retries 20 status 0 \| grep poll_count` (type it twice, 2 s apart) | the number grows by tens of millions | "Core 1 runs its own loop by itself." |

## PART 3. Normal traffic and the "before" (R and S)

| # | Window | Type | You see | Say |
|---|---|---|---|---|
| 3.1 | R | `alarms` | AUTH_FAIL, REPLAY, ... numbers | "These are the alarm counters of the Secure World. I write them down." |
| 3.2 | S | `python3 laptop_can_send.py --iface can0 1_normal.txt` | `sent 10 CAN-FD frames on can0` | "10 correct messages." |
| 3.3 | R | `alarms` | `frame` +10, no alarm changed | "All accepted, no alarm." (first time after a reboot: one COUNTER_RESYNC, normal start-up) |

## PART 4. Attacks, one file each (S sends, R shows)

After each file type `alarms` (and `evt` to see the time).

| # | S types | The attacker | R shows | Say |
|---|---|---|---|---|
| 4.1 | `python3 laptop_can_send.py --iface can0 2_tamper.txt` | changes a data bit, keeps the old signature | **AUTH_FAIL +1** (`evt`: AUTH_FAIL detail 0) | "Only the Secure World has the key, so only it can see the signature is wrong." |
| 4.2 | `... 3_replay.txt` | sends an old message again | **REPLAY +1**, detail 1 | "Counter too old." |
| 4.3 | `... 4_duplicate.txt` | sends the last message again | **REPLAY +1**, detail 2 | "A duplicate." |
| 4.4 | `... 5_conflict.txt` | same counter, other data, valid signature | **REPLAY +1**, detail 3 | "Even a signed message is refused if its counter is used." |
| 4.5 | `... 6_wrongid.txt` | ID 0x124, not allowed | **MALFORMED +1** | "Wrong format, refused cheaply." |
| 4.6 | `... 7_jump.txt` | counter skips 4 | **COUNTER_GAP +1**, detail 4 | "4 messages are missing; it noticed." |

## PART 5. The time and the timeout (R, after the last file, send nothing)

| # | Type | You see | Say |
|---|---|---|---|
| 5.1 | `age` | `last good message arrived 2.0 s ago` | "The Secure World measures time with its own clock." |
| 5.2 | `sleep 3; age` | `... 5.0 s ago` | "It grows while the bus is silent." |
| 5.3 | `evt` | `TIMEOUT detail 1 ... s ago` | "After 0.3 seconds without a valid message it raised TIMEOUT by itself." |

## PART 6. Load (S sends, R shows)

| # | Window | Type | You see | Say |
|---|---|---|---|---|
| 6.1 | R | `A=$(snap)` | nothing | remember the counters before |
| 6.2 | S | `python3 burst_send.py 8_burst2000.txt retry` | json line: `"sent_ok": 2000`, `"rate_fps": ~3700` | "2000 messages as fast as the bus allows." |
| 6.3 | R (after 5 s) | `gapcheck` | `counter span 2000 \| judged 2000 \| missing 0` | "All 2000 judged, none lost." |
| 6.4 | R | `A=$(snap)` | nothing | before the flood |
| 6.5 | S | `python3 burst_send.py 9_flood12000.txt retry` | json: `"sent_ok": 12000` | "12000 messages: more than the monitor can judge (640 per second)." |
| 6.6 | R (after 12 s, when it stops changing) | `gapcheck` | `counter span 12000 \| judged ~4200 \| missing ~7800` | "judged + missing = 12000. The Secure World counted exactly what was dropped. Nothing disappeared without being counted." |

## PART 7. What if Linux is killed? (R)

| # | Type | You see | Say |
|---|---|---|---|
| 7.1 | `/etc/init.d/S95can_forwarder stop >/dev/null 2>&1 </dev/null` | nothing | "I stop the postman." |
| 7.2 | wait 6 seconds, then `evt` | `TIMEOUT detail 4` and `TIMEOUT detail 2` | "The Secure World noticed: 3 heartbeats were not answered." |
| 7.3 | `/tmp/mon_client --retries 20 status 0 \| grep -o "hb_state [A-Z_]* hb_miss_count [0-9]*"` | `MISSED ... 6` | "It counts the misses itself." |
| 7.4 | `/etc/init.d/S95can_forwarder start >/dev/null 2>&1 </dev/null` then wait 4 s and repeat 7.3 | `ANSWERED ... 0` | "The postman is back and the heartbeat is answered again." |

## PART 8. Close (30 seconds)
"Verified in 31 tests (`FINAL_TEST_REPORT.md`): 0 loss up to 640 messages per second, every drop counted. Limits: it detects but does not block, the keys are prototype keys, the Pi 3 has no memory-protection hardware."

## If something is different
* `alarms: not found` -> paste block A again. `No such file ... mon_client` -> run 0.1 on the laptop (it copies the tools).
* An alarm moved twice -> a file was sent twice; run 0.1 again for fresh files.
* `python3: can't open file` -> you are not in `/tmp/demo` (`cd /tmp/demo`).
* Whole demo in one command instead: `python3 scripts/demo_for_professor.py`.
