# Secure-First Dummy Packet Implementation Plan

## 1. Purpose

This document freezes the software-only prototype that will be implemented
before the external CAN controller is available. It extends the completed
OP-TEE Raspberry Pi work through Step 10 without changing the approved packet
movement in `RPI3_SECURE_CAN_FULL_ROADMAP.md`.

The prototype must demonstrate this security property:

> A raw packet originates and is held in Secure World, is validated and
> classified there, and only an approved normal result may cross into Normal
> World. Linux must not create, inject, or inspect the raw packet.

The dummy source does not prove physical CAN isolation. It is a replaceable
test source for exercising the secure queue, decision, and release path before
CAN hardware is available.

## 2. Frozen Architecture

```text
Raspberry Pi secure timer
        |
        | secure interrupt
        v
OP-TEE core dummy packet source
        |
        v
Bounded Secure World RX queue
        |
        v
Secure validation and classification
        |
        +-- CRITICAL --> secure handling/counter/log
        |
        +-- INVALID ---> secure rejection/counter/log
        |
        +-- NORMAL ----> sanitized approved-release queue
                                      |
                                      | GET_APPROVED_PACKET
                                      v
                            Normal World application
```

The Linux application is a consumer only. Its request may retrieve a result
that has already been generated, validated, and classified. A Linux request
must never generate the next raw packet or advance the dummy packet source.

## 3. Execution Model

1. TF-A starts OP-TEE before Linux during boot.
2. OP-TEE initializes the dummy source, secure queues, policy, counters, and
   timer.
3. Each secure timer event creates one dummy CAN packet in Secure World.
4. Secure World records its arrival time and places it in the secure RX queue.
5. Secure processing validates and classifies queued packets in FIFO order.
6. Critical and invalid packets remain in Secure World as counters/log records.
7. Only a sanitized valid-normal result enters the approved-release queue.
8. The Normal World client polls for approved results and displays them.

A normal OP-TEE user TA is request-driven and cannot provide the independent
producer by itself. The timer source and secure RX state therefore belong in
OP-TEE core/platform code. A pseudo-TA or narrowly defined TA-facing service
will expose approved results and statistics.

The initial implementation will use one ordered secure processor. It will not
attempt parallel packet validation because replay and log state must remain
deterministic.

### 3.1 Frozen Two-Core Constraint

The demonstration will use only two of the Raspberry Pi 3's four physical CPU
cores. Cores 2 and 3 will not participate in either the packet pipeline or the
Normal World demonstration.

```text
CPU 0: Normal World Linux consumer and required Linux housekeeping
CPU 1: preferred target for Secure World timer/packet processing
CPU 2: disabled/not brought online for the demonstration
CPU 3: disabled/not brought online for the demonstration
```

The Normal World application will be pinned to CPU 0. The secure timer
interrupt and ordered secure packet path will be targeted to CPU 1 where the
RPi3 interrupt controller and TF-A/OP-TEE port permit this.

This is a workload assignment, not a claim that the processor contains
separate Secure World and Normal World CPUs. TrustZone security state belongs
to each physical core. CPU 1 may be in Normal World while idle and switch into
Secure World on a secure interrupt. While CPU 1 processes a new secure packet,
CPU 0 may simultaneously process an older approved result in Linux.

The target pipeline is:

```text
CPU 1 / Secure World: packet 1 classify -> packet 2 classify -> packet 3 classify
CPU 0 / Normal World:                    packet 1 consume -> packet 2 consume
```

The current image was built with `CFG_TEE_CORE_NB_CORE=4` and
`CFG_NUM_THREADS=4`. The implementation must deliberately change and verify
the effective two-core configuration. Candidate controls are not frozen until
Phase A verifies them:

- build OP-TEE for two participating cores/threads where supported;
- limit Linux to two online cores through boot/device-tree configuration;
- pin the consumer with CPU affinity;
- route the secure timer interrupt to the intended CPU;
- prove through core-ID traces that cores 2 and 3 execute no demo work.

A strict permanently Secure-only CPU is not assumed. Achieving static physical
core ownership would require additional TF-A/PSCI/platform work and is outside
this prototype unless Phase A proves that it is required and supported.

## 4. Packet and Result Formats

All shared interfaces must use fixed-width integer types, explicit lengths,
and version fields. Raw Secure World packets and released Normal World results
are deliberately different structures.

```c
#define DEMO_PACKET_VERSION       1U
#define DEMO_MAX_PAYLOAD          8U

struct demo_raw_packet {
    uint32_t version;
    uint32_t source_id;
    uint32_t can_id;
    uint32_t counter;
    uint32_t flags;
    uint32_t payload_length;
    uint8_t payload[DEMO_MAX_PAYLOAD];
};

enum demo_decision {
    DEMO_DECISION_ACCEPT = 1,
    DEMO_DECISION_REJECT = 2,
};

enum demo_reason {
    DEMO_REASON_VALID = 1,
    DEMO_REASON_ID,
    DEMO_REASON_COUNTER,
    DEMO_REASON_FORMAT,
    DEMO_REASON_QUEUE_OVERFLOW,
};

enum demo_class {
    DEMO_CLASS_NORMAL = 1,
    DEMO_CLASS_CRITICAL = 2,
};

struct demo_approved_result {
    uint32_t version;
    uint32_t secure_sequence;
    uint32_t source_id;
    uint32_t can_id;
    uint32_t counter;
    uint32_t decision;
    uint32_t reason;
    uint32_t packet_class;
    uint32_t payload_length;
    uint8_t payload[DEMO_MAX_PAYLOAD];
};
```

Structure sizes, alignment, and byte order must be checked at build time.
Shared-memory inputs from Linux must never be interpreted as trusted Secure
World state.

## 5. Initial Dummy Traffic and Policy

The source will repeatedly generate a deterministic sequence so test output is
reproducible:

| Sequence | CAN ID | Condition | Expected result |
| --- | --- | --- | --- |
| 1 | `0x100` | Valid critical | Retain securely |
| 2 | `0x200` | Valid normal | Release sanitized result |
| 3 | `0x999` | Unknown ID | Reject securely |
| 4 | `0x201` | Valid normal | Release sanitized result |
| 5 | `0x100` | Repeated counter test | Reject as replay/counter |
| 6 | `0x200` | Invalid length | Reject as format |

Initial policy:

- `0x100` through `0x10f`: critical;
- `0x200` through `0x20f`: normal;
- every other CAN ID: reject;
- payload length greater than 8: reject;
- counters must increase for each source;
- rejected and critical raw payloads are not returned to Linux.

HMAC, trusted timestamps, rate detection, persistent secure storage, and a
hash-chained log remain later roadmap work. This prototype must not claim that
those controls are implemented.

## 6. Queues and Overflow Rules

Two bounded FIFO ring buffers will be used:

```text
Secure RX queue:        64 raw packets
Approved release queue: 64 sanitized results
```

Each queue records:

- read and write indexes;
- current and maximum depth;
- total enqueued and dequeued entries;
- overflow count;
- last secure sequence number.

Initial overflow policy:

- never overwrite an unprocessed entry;
- drop the newest arrival when a queue is full;
- increment an overflow counter;
- record an overflow reason in Secure World;
- continue operating without unbounded allocation.

The queue size is an initial test value, not a final performance claim.

## 7. Timer and Scheduling Rules

The timer will initially generate one packet every 100 ms. After correctness is
established, test intervals will be reduced in this order:

```text
100 ms, 50 ms, 10 ms, 5 ms, 1 ms
```

The secure interrupt handler must remain bounded. It should timestamp and
capture/generate the minimum event, enqueue it, acknowledge the interrupt, and
return. HMAC, storage writes, large logs, and terminal printing must never be
placed in the top-half handler.

Before implementation, the selected Raspberry Pi 3 OP-TEE/TF-A timer and
interrupt route must be verified. If a suitable periodic secure interrupt
cannot safely drive deferred processing on this port, implementation must stop
at that checkpoint and document the required OP-TEE core scheduling change.
It must not silently fall back to Linux-triggered packet generation.

## 8. Secure-to-Normal Interface

The existing Step 10 challenge command will be retained as a platform
communication test. New commands will be added:

```text
GET_APPROVED_PACKET
GET_SECURE_STATISTICS
RESET_DEMO_STATE        (development/testing only)
```

`GET_APPROVED_PACKET` has these semantics:

- it never creates a raw packet;
- it never advances the dummy source;
- it returns one previously approved normal result if available;
- otherwise it returns `NO_PACKET_AVAILABLE`;
- it never returns the raw payload of critical or rejected packets.

`GET_SECURE_STATISTICS` returns counters only:

```text
generated
captured
processed
accepted_normal
retained_critical
rejected
rx_overflow
release_overflow
maximum_rx_depth
maximum_release_depth
```

Linux polling is the initial release mechanism. A future notification driver
may replace polling, but notification is not required to prove secure-first
classification.

## 9. Timing and Cycle Measurements

Secure World will timestamp:

```text
T0: source/timer event
T1: raw packet captured in secure queue
T2: validation start
T3: validation end
T4: classification end
T5: secure retention or normal-release completion
```

Required metrics:

```text
capture latency      = T1 - T0
queue waiting time   = T2 - T1
validation time      = T3 - T2
classification time  = T4 - T3
output time          = T5 - T4
total secure latency = T5 - T0
inter-arrival time   = current T0 - previous T0
```

Cycle counts will come from an architected counter available to Secure World.
They will be converted using the counter frequency instead of assuming a CPU
clock. Report minimum, average, maximum, and 95th/99th percentile where the
sample count permits.

For sustained operation, average service time must be less than average
inter-arrival time. A queue may absorb a temporary burst but must not be used
to hide a permanently overloaded design.

## 10. Normal World Application

The Linux application will:

1. start automatically or manually after Linux boots;
2. open one long-lived OP-TEE session;
3. poll only for approved packets;
4. display the secure sequence and sanitized normal packet;
5. periodically display aggregate secure statistics;
6. distinguish `NO_PACKET_AVAILABLE` from an error;
7. reconnect cleanly if the session is interrupted;
8. never accept a command-line option that injects a raw packet into this demo.

Example output:

```text
Normal World consumer started
No approved packet available
Approved: secure_seq=2 id=0x200 counter=2 data=11:22:33:44
Approved: secure_seq=4 id=0x201 counter=4 data=55:66:77:88
Stats: generated=6 normal=2 critical=1 rejected=3 dropped=0
```

Secure traces and statistics provide evidence for critical/rejected packets;
their raw contents must not be printed by the Normal World client.

## 11. Implementation Phases

### Phase A — Feasibility checkpoint

- identify the Raspberry Pi 3 secure timer and interrupt;
- confirm OP-TEE/TF-A routing and registration support;
- restrict the demonstration to CPU 0 and CPU 1;
- pin the Normal World consumer to CPU 0;
- target secure timer/packet work to CPU 1 where supported;
- verify that CPU 2 and CPU 3 perform no demonstration work;
- demonstrate an independent secure counter increment with Linux idle;
- record evidence that the counter advances without a client invocation.

Exit condition: a Secure World event occurs independently of Normal World on
the intended core, the client runs on its assigned core, and only two cores
participate in the demonstration.

### Phase B — Core dummy source and queues

- implement the deterministic generator;
- implement bounded secure RX and release queues;
- implement ordering, counters, and overflow behavior;
- add unit-style tests for wraparound and full queues.

Exit condition: packets are generated and queued without Linux creating them.

### Phase C — Validation and classification

- implement format, ID, and counter checks;
- implement critical, normal, and rejection decisions;
- ensure only valid normal results enter the release queue.

Exit condition: all frozen dummy vectors produce their expected decisions.

### Phase D — Client interface

- extend the TA/pseudo-TA command header;
- implement approved-result and statistics commands;
- change the Normal World client into a continuous consumer;
- prove polling does not change the generated counter.

Exit condition: Linux displays normal results and cannot retrieve protected raw
critical/rejected packets.

### Phase E — Performance and robustness

- add cycle/timestamp measurements;
- run every planned timer interval;
- test queue overflow and recovery;
- test continuous execution and client restart;
- preserve runtime logs.

Exit condition: no unexplained loss, corruption, reordering, or deadlock.

### Phase F — Persistent Pi image and evidence

- integrate components into the Buildroot overlay;
- rebuild the affected OP-TEE, boot, rootfs, and SD-card artifacts;
- flash the test card and verify physical boot;
- capture secure traces, Linux output, hashes, and test results;
- add a completion record without marking physical CAN steps complete.

Exit condition: the demonstration survives reboot and runs from the generated
image.

## 12. Acceptance Tests

The dummy prototype is complete only when all tests pass:

- Secure World initializes before Linux.
- Only the two selected cores participate in the demonstration.
- Runtime core-ID evidence matches the frozen CPU assignment.
- The secure generated counter advances without a Linux client invocation.
- Linux cannot submit a raw packet through the demo interface.
- Critical packets never appear in Normal World as released payloads.
- Invalid ID, format, and counter packets are rejected.
- Valid normal packets arrive in FIFO order with secure sequence numbers.
- Client polling does not generate or classify a new source packet.
- Client stop/restart does not reset protected state unexpectedly.
- Queue-full behavior is bounded and counted.
- Timing and maximum queue depth are reported.
- The generated SD-card image contains the tested binaries.

## 13. Evidence and Claims

The final prototype evidence will include:

- secure initialization and timer traces;
- proof of secure counter progress while the client is absent;
- classification counters;
- Normal World approved-packet output;
- queue/overflow test results;
- cycle and latency measurements;
- source and image hashes.

Allowed claim:

> The software prototype generates simulated packets from a Secure World timer,
> validates and classifies them in OP-TEE, retains critical/rejected events,
> and releases only sanitized normal results to Linux.

Claims that are not allowed until external hardware work is complete:

- physical CAN packets enter Secure World first;
- Linux is hardware-blocked from the CAN controller;
- secure GPIO/SPI/CAN reception is implemented;
- the Raspberry Pi prototype provides production automotive isolation.

## 14. CAN Hardware Replacement Boundary

When hardware becomes available, replace:

```text
secure timer interrupt
    -> deterministic dummy packet generator
```

with:

```text
CAN controller interrupt
    -> Secure World GPIO handler
    -> Secure World SPI/CAN receive driver
    -> decoded raw CAN packet
```

Keep, subject to measured hardware constraints:

- secure RX queue;
- validation and classification policy;
- secure sequence and statistics;
- critical/rejected handling;
- approved-release queue;
- Normal World consumer interface;
- timing framework.

The physical implementation must still complete Roadmap Steps 11 through 18
and must independently prove that Linux cannot own or read the selected CAN
controller.

## 15. Estimated Schedule

| Work | Estimate |
| --- | --- |
| Secure timer/interrupt feasibility | 1–3 days |
| Dummy source and secure queues | 2–3 days |
| Validation and classification | 1–2 days |
| Result interface and Linux consumer | 1–2 days |
| Timing, overload, and restart testing | 2–3 days |
| Image integration and evidence | 1–2 days |

Expected total: 8–15 working days. Timer/interrupt limitations in the Raspberry
Pi 3 OP-TEE port may extend this estimate and must be resolved honestly rather
than changing the packet direction.

## 16. Immediate Next Action

Begin Phase A only. Inspect the current Raspberry Pi 3 OP-TEE and TF-A platform
timer/interrupt and multicore implementation. First prove the two-core
assignment and then prove that a Secure World counter advances on the intended
core without any Normal World invocation. Do not implement packet
classification until both conditions have passed.
