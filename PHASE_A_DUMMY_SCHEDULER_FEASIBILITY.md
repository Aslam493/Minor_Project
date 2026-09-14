# Phase A Dummy Scheduler Feasibility and Recovery Record

Date checked: 2026-09-14

## Purpose

This record preserves the dummy-scheduler checkpoint so failed experiments can
be diagnosed or rolled back without losing the known-good Step 10 state.
Dummy traffic must originate in Secure World independently of Linux.

## Known-good baseline

- Steps 1 through 10 are complete.
- Raspberry Pi boots Linux and OP-TEE 4.10.
- `/dev/tee*`, `tee-supplicant`, `xtest`, and the project TA were tested.
- The `secure_can_demo` challenge/response test passes.
- Evidence is in `Step10_Verification/STEP10_COMPLETION.md` and
  `Step10-runtime.log`.

## Phase A findings

The running Raspberry Pi reported:

```text
Linux: buildroot 6.7.0-rc6-v8 aarch64
online CPUs:  0-3
present CPUs: 0-3
OP-TEE revision: 4.10 (d5001fe0af66a4c0)
```

The generated OP-TEE configuration contains:

```text
CFG_TEE_CORE_NB_CORE=4
CFG_NUM_THREADS=4
CFG_CALLOUT=n
CFG_CORE_ASYNC_NOTIF=n
```

The RPi3 platform enables `CFG_SECURE_TIME_SOURCE_CNTPCT`, allowing Secure
World to read the architectural counter. It does not create a periodic
interrupt. The platform does not register an OP-TEE interrupt controller or
call `timer_init_callout_service()`.

Result: Phase A is incomplete. The current image cannot claim an independent
100 ms OP-TEE producer or a two-core demonstration.

## Planned dummy structure

```text
Secure 100 ms timer
        -> deterministic raw-packet generator
        -> 64-entry Secure RX FIFO
        -> ordered validation and classification
             |-- critical: retain/count in Secure World
             |-- invalid: reject/count in Secure World
             +-- normal: sanitized result
                              -> 64-entry release FIFO
                              -> Linux read-only consumer
```

## Next implementation sequence

1. Add or port RPi3 interrupt-controller integration for OP-TEE.
2. Route the secure physical timer interrupt through TF-A to OP-TEE.
3. Enable and initialize the OP-TEE callout service.
4. Test a bounded 100 ms callback that increments a protected counter only.
5. Build OP-TEE with two cores and two threads.
6. Limit Linux to CPUs 0 and 1 and pin the consumer to CPU 0.
7. Prove the counter advances without a client and record the handler core ID.
8. Only then implement packets, queues, and classification.

## Failure handling and recovery

1. Do not overwrite the known-good Step 10 SD card or image.
2. Use separate experimental artifacts and preferably a separate test SD card.
3. Keep every experiment in a dedicated Git commit.
4. Capture serial UART output from power-on through failure.
5. Record the commit, command, hashes, configuration, and first failing line.
6. Revert only the experimental commit; preserve unrelated user files.
7. Restore the known-good Step 10 image if the experimental image cannot boot.

Phase A is complete only after independent-counter, two-core, core-ID, client
restart, reboot, and recovery evidence is preserved.
