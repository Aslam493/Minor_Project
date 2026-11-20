# Real-bus test scripts (2026-11-20/21)

Exactly the scripts that produced `baseline/real_bus_*`. They contain no key. Paths to the scratch folder and the two Pi addresses
(sender 10.237.27.170, receiver 10.237.27.187) are as used that night; edit them before reuse. Packet files are made on the laptop by
`tools/monitor_client/packet_gen.py` (the prototype K_frame is read from the Secure World source there, never copied to a Pi) and replayed
on the sender by `tools/monitor_client/laptop_can_send.py`.

| File | Step |
|---|---|
| step3.sh | raw frames sender -> receiver (10 classic + 10 FD), plus the reverse test was done by hand (see baseline/real_bus_step3c) |
| load_sender.py, step4.sh | 1000 FD frames at 100/s |
| step5.sh | 600 tagged packets, 10/s, into the monitor (Mode P, `can_forwarder --pull`) |
| make_attacks.py, step6.sh | the six attacks |
| can_forwarder_test_drop_every.patch | TEST-ONLY patch to a scratch copy of can_forwarder.c (env FWD_TEST_DROP_EVERY=N); not applied to the repo |
| step7.sh | 7a, 7b, 7c first run |
| step7_clean.sh, step7c4.sh | 7b clean rerun, 7c corrected run |
| step7_rerun_INVALID_poller_load.sh | the rerun that polled status twice per 0.3 s; INVALID, kept as a record (see the report) |
