# Watch the system yourself: three terminal windows

You send CAN-FD packets from the **sender Pi** over the real bus, see the raw frames on the **receiver**, and see what Secure World decides.
Everything below is a command you type; under each one is what you should see, with a **real line from the runs of 2026-11-20/21** (`baseline/real_bus_*`).

Current addresses (DHCP, they can change after any reboot; check your router for MAC `b8:27:eb:1e:98:02` = receiver):
**receiver `10.237.27.77`** (Mode P image with N-04), **sender `10.237.27.144`**.
`bash scripts/watch_live.sh` prints just the commands of this file and copies the tools; `RX=... SN=... bash scripts/watch_live.sh` uses other addresses.

## Four rules so you do not disturb what you are watching

1. **Status every 2 s at most, one `mon_client` at a time.** Tonight a status poller that opened two monitor sessions every 0.3 s made Secure World's replies late (`LATE_ANSWER` / `STALE_ANSWER` floods, `baseline/real_bus_step7b_c_rerun_INVALID_poller_load_20261121.txt`). If you see those events, you are loading the system.
2. **Events are limited to one per second per class** (a `TIMEOUT` of any detail shares one slot). The counters on status page 1 are always exact, the event list can skip repeats. Do not fire attacks faster than one per second.
3. **Run the packet files in the order of their numbers.** The monitor only accepts a counter that follows the last accepted one. If you skip or repeat a step, run `bash scripts/watch_live.sh attacks` (reads the monitor's `last_counter` and makes fresh attack files).
4. **A `TIMEOUT` (traffic, detail 1) about 300 ms after you stop sending is normal**: it is the traffic deadline.

What was and was not run: every command comes from tonight's runs **except**: the status loop (new, one call per 2 s), running the sender in the foreground, and the numbered packet files (`tools/monitor_client/watch_packets.py`, new). Those were only tested **on the laptop** (all packet files pass the independent `verify_packets.py` check; the counter sequence was checked in code). Nothing of this document was run on the Pis when it was written.

## 0. After a reboot of the receiver: copy the tools (laptop, one scp)

`/tmp` on the receiver is a RAM disk: after a reboot it is empty. Run this on the laptop:

```sh
cd ~/Minor_Project/Part_E_Verification
scp tools/monitor_client/mon_client tools/monitor_client/can_forwarder tools/canmode/canmode tools/canutils/candump_aarch64 root@10.237.27.77:/tmp/
ssh root@10.237.27.77 'chmod +x /tmp/mon_client /tmp/can_forwarder /tmp/canmode /tmp/candump_aarch64'
```

You see: one progress line per file, each ending in `100%`; the second command prints nothing. `candump_aarch64` is the static `candump` used for tonight's captures (`tools/canutils/README.md`); `mon_client` reads the monitor, `can_forwarder` is the Mode P forwarder with the heartbeat responder, `canmode` reads the CAN settings.

### 0b. Make the packet files and put them on the sender (laptop)

```sh
bash scripts/watch_live.sh prepare
```

This copies the tools (as above), reads `last_counter` from the monitor (read-only), makes the numbered files on the laptop (the key stays on the laptop; the files hold only counters, payloads and tags), and copies them with `laptop_can_send.py` to `/tmp/watch/` on the sender. You see lines like:

```
wrote 1_healthy.txt  600 packet(s)
wrote 2_pause2s.txt   10 packet(s)
wrote 3_badtag.txt     1 packet(s)
```

After a **reboot of the receiver** `last_counter` is 0 and the first packet is `COUNTER_RESYNC`. Run `prepare` again whenever the receiver rebooted. To remake only the attack files from the monitor's current counter (after you stopped the healthy stream early, or repeated a step):

```sh
bash scripts/watch_live.sh attacks
```

## Window 2: raw CAN on the receiver (start this first)

Check the CAN settings first (optional), then watch the wires. `-td` shows the time since the previous frame, `-x` shows the flags (`B` = bit-rate switch of CAN-FD).

```sh
ssh root@10.237.27.77
/tmp/canmode can0 | grep -E "mtu|nominal|data bit|FD mode|controller state"
/tmp/candump_aarch64 -td -x can0
```

**`canmode` shows:** FD mode ENABLED and both bit rates, e.g. `data bit timing        2000000 bit/s  sample-point 75.0%  tq 25 ns  prop_seg 7  phase_seg1 7  phase_seg2 5  sjw 2  brp 1` and `FD mode                ENABLED` (tonight: `baseline/fd_permanent_after_reboot_20261121.txt`).
**`candump` shows** one line per frame, nothing while the sender is silent (stop it with Ctrl+C). An FD frame of the monitor has `B` in the flags column and `[48]` bytes; the first 4 data bytes are the packet counter, little-endian (counter 601 = `59 02 00 00`). A real FD line from tonight (a different payload, same layout):

```
 (000.009582)  can0  RX B -  123  [48]  00 00 00 01 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 ...
```

A real gap shows in the first column, e.g. ` (001.028629)  can0  RX B -  123  [48]  01 11 12 ...` = 1.03 s after the frame before. Classic frames look like ` (000.059218)  can0  RX - -  123   [4]  DE AD BE EF`.

## Window 3: forwarder, status and events on the receiver

### 3.1 Start the Mode P forwarder with the heartbeat responder

It keeps the frames it receives in a queue and hands them to Secure World only when Secure World asks; it also answers the heartbeat. 900 s = 15 minutes; it runs in the background, its report goes to `/tmp/fwd.log`.

```sh
ssh root@10.237.27.77
/tmp/can_forwarder run 900 --pull --verbose > /tmp/fwd.log 2>&1 & echo $! > /tmp/fwd.pid
```

You see: the prompt returns at once, with a line like `[1] 612`. Nothing else until packets arrive.

### 3.2 Safe status watch: one reading every 2 s (stop with Ctrl+C)

```sh
while true; do date +%T; /tmp/mon_client --retries 20 status 0 | sed -n 1,7p; echo; sleep 2; done
```

You see the monitor's page 0 every ~2.3 s. What to look at: `last_counter` rises with every accepted packet; `hb_state ANSWERED` and `hb_miss_count 0` mean the forwarder answers the heartbeat; `traffic_age_ticks` divided by 19,200,000 is the seconds since the last accepted frame (under 0.3 s while a 10/s stream runs); `health:` must keep `init_ok=1 starved=0 pta_lock_timeouts=0`. A real page from tonight (taken after the forwarder had stopped, which is why `hb_state` says MISSED):

```
page 0: version 1 length 128 boot_id 0xca41ea7f flags 0x1f mode 2
  now_ticks 7355687407 core1_poll_age_ticks 3319 poll_count 13303371645
  skip_count 0 skip_total 210 pta_lock_timeouts 0 pta_timeouts_seen 0
  last_counter 600 hb_seq 365 hb_state MISSED hb_miss_count 3 idle_uses 0
  traffic_age_ticks 314849845 hb_last_ok_age_ticks 71430930 lease_remaining_ticks 0
  ev_seq 13 ev_ack 0 ev_lost 0 pinned_used 5
  health: core1 last poll 0 ms ago, init_ok=1 starved=0 skip_count=0 pta_lock_timeouts=0 skip_total=210
```

`mode 2` = Mode P. `skip_total` counts how often Core 1 found the lock busy; it grows while packets flow and is harmless as long as `skip_count` stays 0.

### 3.3 The events (after Ctrl+C of the status loop)

```sh
/tmp/mon_client --retries 20 events --all
```

You see every event not yet acknowledged, five per page, ending with `returned N, more=0, ev_lost=0` and a `health:` line. When the forwarder starts you first see `HEARTBEAT_RECOVERED` (real: `seq 8 HEARTBEAT_RECOVERED  counter 5632 detail 1 ticks 5791686935`). Real lines from tonight, one per attack in section Window 1 below:

```
seq 9 COUNTER_RESYNC       counter 1 detail 0 ticks 5890737110
seq 10 AUTH_FAIL            counter 601 detail 0 ticks 7042669769
seq 12 REPLAY               counter 615 detail 1 ticks 32538341915
seq 16 COUNTER_GAP          counter 625 detail 4 ticks 32554963281
seq 23 TIMEOUT              counter 0 detail 1 ticks 34416245880
seq 24 TRAFFIC_RECOVERED    counter 0 detail 1 ticks 34458529529
```

To see only new events, read `ev_seq` from the status (say 13) and ask for the following ones; to empty the ring after reading, acknowledge:

```sh
/tmp/mon_client --retries 20 events --from 14 --all
/tmp/mon_client --retries 20 ack 13
```

The second command prints a line like `acknowledged up to 13 (effective 13)`. (The ring holds 40 ordinary events; acknowledging old ones keeps it from overwriting unread ones.)

### 3.4 Stop the forwarder and read its report

```sh
kill $(cat /tmp/fwd.pid); sleep 1; tail -12 /tmp/fwd.log
```

You see the summary lines. Real ones from the 60 s run of tonight:

```
PULL: frames received from can0 and queued: 601 | requests answered: 1551 (ON_TIME 1551, LATE 0, STALE 0, UNISSUED 0, DUPLICATE 0) | packets sent in replies and judged: 601 | still queued: 0 | dropped by the queue: 0
heartbeat answers: ON_TIME 79 LATE 0 other 0
end state: flags 0x0f last_counter 600 hb_state ANSWERED miss_count 0 starved 0
```

While it runs, `tail -f /tmp/fwd.log` (Ctrl+C to leave) shows one line per handed-over packet, e.g. `  request 5735: sent 1 packet(s) -> ON_TIME, Secure World judged 1 (queue left 0)`. `LATE_ANSWER` or `STALE_ANSWER` in the events would mean replies took longer than 40 ms.

## Window 1: the sender

```sh
ssh sender@10.237.27.144
```

### 1.1 Check the sender's CAN settings

```sh
ip -details link show can0 | grep -E "can <|state|bitrate|dbitrate"
```

You see FD on, both bit rates and no bus error (real, from tonight):

```
    can <FD,TDC-AUTO> state ERROR-ACTIVE (berr-counter tx 0 rx 0) restart-ms 0 
	  bitrate 500000 sample-point 0.875
	  dbitrate 2000000 dsample-point 0.750
```

### 1.2 Start the sender: 600 packets, 10 per second, 60 s (start windows 2 and 3 first)

```sh
python3 /tmp/watch/laptop_can_send.py --iface can0 /tmp/watch/1_healthy.txt
```

You see nothing for 60 s, then `sent 600 CAN-FD frames on can0 (no key, no HMAC in this script)`. **Window 2:** about 10 FD lines per second, each about 100 ms after the previous one (`-td`). **Window 3:** `last_counter` rises by 10 per second, `hb_state ANSWERED`; the forwarder log shows `ON_TIME` lines. **Events:** one `COUNTER_RESYNC` for the very first packet after a reboot of the receiver (real: `seq 9 COUNTER_RESYNC counter 1`), nothing else; when the stream ends a `TIMEOUT detail 1` follows about 300 ms later.

### 1.3 Stop sending for 2 s, then go on

```sh
python3 /tmp/watch/laptop_can_send.py --iface can0 /tmp/watch/2_pause2s.txt
```

You see `sent 10 CAN-FD frames ...`. **Window 2:** 5 frames, then one line with a delta of about **2.0 s**, then 5 frames. **Events:** `TIMEOUT detail 1` about 300 ms into the silence, then `TRAFFIC_RECOVERED` when the next packet arrives (real, from a 2.5 s silence: `seq 23 TIMEOUT counter 0 detail 1` then `seq 24 TRAFFIC_RECOVERED counter 0 detail 1`, 319 ms after the last frame). If you start this file more than 300 ms after the previous run ended, you also see a `TRAFFIC_RECOVERED` for its very first packet, and another `TIMEOUT` after its last one. No `COUNTER_GAP`: the counters stay consecutive.

### 1.4 Bad tag: one flipped bit in the authentication tag

```sh
python3 /tmp/watch/laptop_can_send.py --iface can0 /tmp/watch/3_badtag.txt
```

You see `sent 1 CAN-FD frames ...`. **Window 2:** one frame. **Events:** `AUTH_FAIL` with the counter of that packet (real: `seq 10 AUTH_FAIL counter 601 detail 0`). `last_counter` does **not** move: a rejected packet changes nothing.

### 1.5 Replay: an old counter

```sh
python3 /tmp/watch/laptop_can_send.py --iface can0 /tmp/watch/4_replay.txt
```

**Events:** `REPLAY` with the old counter and **detail 1** (older than the last accepted one). Real: `seq 12 REPLAY counter 615 detail 1`. (Detail 2 = same packet again, detail 3 = same counter with a different payload, real: `seq 13 REPLAY counter 620 detail 3`.) `last_counter` does not move.

### 1.6 Counter jump: five packets skipped

```sh
python3 /tmp/watch/laptop_can_send.py --iface can0 /tmp/watch/5_jump.txt
```

**Events:** `COUNTER_GAP` with the new counter and `detail` = counter minus last minus 1. This file jumps by 6, so expect **detail 5**. Real line from tonight, with a jump of 5: `seq 16 COUNTER_GAP counter 625 detail 4`. The jump is accepted, so `last_counter` moves up by 6, and a `TRAFFIC_RECOVERED` appears if traffic had timed out before.

## Suggested order

| Step | Window | What |
|---|---|---|
| 1 | 2 | `candump` running |
| 2 | 3 | forwarder started (3.1), then the status loop (3.2) |
| 3 | 1 | 1.1 check, then 1.2 healthy stream (60 s) |
| 4 | 1 | when 1.2 has finished: 1.3 pause, 1.4 bad tag, 1.5 replay, 1.6 jump (one every few seconds) |
| 5 | 3 | Ctrl+C the status loop, 3.3 events, 3.4 stop the forwarder |

## If something looks wrong

| You see | Likely reason |
|---|---|
| Window 2 shows nothing | sender not running, or its `can0` is classic (check 1.1 shows `FD`), or the wires/termination |
| `hb_state MISSED` and `TIMEOUT detail 2` | the forwarder is not running (it answers the heartbeat) |
| `REPLAY` or `COUNTER_GAP` where you expected nothing | the receiver rebooted or you skipped a file: run `bash scripts/watch_live.sh prepare` (after a reboot) or `attacks` |
| `LATE_ANSWER` / `STALE_ANSWER` | too much load on the monitor (status faster than every 2 s, a second `mon_client` loop) |
| `ssh` refuses or times out | the address changed (DHCP): find the new one in the router |
