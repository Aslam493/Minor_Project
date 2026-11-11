# Read-only Pi check, 2026-11-12, 10.237.27.187 (SSH only, no UART, nothing flashed)

- Image: boot partition armstub8.bin sha256 775ce384...721d5 = `armstub8.monitor-hardened-c16384.bin` (hardened production profile). Card = 15,097,856 sectors = the 8 GB spare. Original 32 GB card not in use.
- `tools/verify_boot/verify_boot.sh 10.237.27.187`: 16 pass, 0 FAIL, 2 skipped (canmode and ta_roundtrip not on the Pi). Uptime 218 s at the time (just booted); OP-TEE 4.10; CPU mask 0,2-3 (Core 1 diverted); Core 1 heartbeat rising; FIQ not armed; can0 up, mcp251xfd found chip, no SPI CRC errors.
- Monitor (`mon_client status 0/1`, `events`, copied to /tmp then removed): init_ok=1, starved=0, Core 1 polling (poll_count 9,157,618,696, last poll 0 ms ago), skip_count 0, pta_lock_timeouts 0, ev_lost 0.
- Heartbeat: hb_state OPEN, hb_seq 252, hb_miss_count 251, one event `TIMEOUT detail 3` (boot-grace expiry). Expected: no Normal-World responder is running, so challenges go unanswered. Not a fault.
- traffic_age and hb_last_ok_age = 2^64-1 (never seen traffic or an OK answer since boot).
- NOT checked here: responder answering (board cmd 22/23 run), real CAN traffic, UART boot log.
