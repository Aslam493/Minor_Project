# Secure CAN Gateway — Final Design Flow Diagram (Core1 + Core2 Split)

## Full Flow Diagram

```
┌────────────────────────────────────────────────────────────────────────┐
│ [0] CAN BUS (physical wire)                                             │
│     A CAN-FD frame arrives on the wire                                  │
└──────────────────────────────┬───────────────────────────────────────── ┘
                                │
                                ▼
┌────────────────────────────────────────────────────────────────────────┐
│ [1] MCP2518FD CAN-FD CONTROLLER CHIP (external, over SPI0)              │
│     Chip receives the frame, stores it in its internal Message RAM,    │
│     sets RXIF in its RX FIFO status, and asserts its /INT pin           │
│     (physically wired to GPIO25 on the Pi)                              │
└──────────────────────────────┬────────────────────────────────────────┘
                                │  GPIO25 goes low (electrical signal)
                                ▼
┌────────────────────────────────────────────────────────────────────────┐
│ [2] BCM2837 INTERRUPT CONTROLLER (VideoCore IRQ/FIQ hardware)           │
│     GPIO25's interrupt is routed as an FIQ (not a normal IRQ) because  │
│     FIQ_CONTROL was previously armed for Secure World. This routing    │
│     decision is hardware register state, not software polling.        │
└──────────────────────────────┬────────────────────────────────────────┘
                                │  FIQ exception taken
                                ▼
┌────────────────────────────────────────────────────────────────────────┐
│ [3] CORE 1 (ARM Cortex-A53 core #1, permanently diverted into           │
│     Secure World / OP-TEE at boot via TF-A's PSCI CPU_ON intercept)     │
│                                                                          │
│     interrupt_main_handler() — FIQ context, capture only:              │
│     3a. wait_for_spi_idle()         — HARDWARE: polls BCM2835 SPI0's   │
│                                        SPI_CS.TA bit — bus-safety check│
│     3b. Transaction 1 (SPI read)    — HARDWARE: SPI0 peripheral shifts│
│         FIFOUA + INT register bytes   bits to/from the MCP2518FD over │
│                                        MOSI/MISO/CLK, CS line asserted│
│     3c. Transaction 2 (SPI read)    — HARDWARE: SPI0 peripheral shifts│
│         60-byte CAN-FD message object  the actual frame bytes in      │
│     3d. Core1 increments its own    — SOFTWARE (Core1 CPU register):  │
│         ground-truth frame counter    this is the one and only place  │
│                                        this counter is ever written   │
└──────────────────────────────┬────────────────────────────────────────┘
                                │
                   ┌────────────┴─────────────┐
                   ▼                           ▼
┌───────────────────────────────┐   ┌──────────────────────────────────┐
│ [4A] FAST PATH → LINUX          │   │ [4B] SECURE PATH → CORE 2         │
│                                 │   │                                  │
│ HARDWARE: write to a shared    │   │ HARDWARE: write to a second,     │
│ struct in ordinary DRAM        │   │ separate shared struct in        │
│ (coherent, cache-visible to    │   │ Secure-World-only RAM (never     │
│ both Secure and Normal World)  │   │ visible to Linux's MMU mappings) │
│                                 │   │                                  │
│ SOFTWARE: dsb() memory barrier │   │ SOFTWARE: dsb() memory barrier   │
│ (ARM instruction, Core1)       │   │ (ARM instruction, Core1)         │
│                                 │   │                                  │
│ HARDWARE: write to ARM_LOCAL   │   │ HARDWARE: write to ARM_LOCAL     │
│ mailbox 1 register for Core0's │   │ mailbox 1 register for Core2's   │
│ position — this is a real      │   │ position — same mailbox hardware,│
│ memory-mapped doorbell built   │   │ different target core            │
│ into the BCM2837 SoC           │   │                                  │
└───────────────┬─────────────────┘   └──────────────┬───────────────────┘
                │                                     │
                ▼                                     ▼
┌───────────────────────────────┐   ┌──────────────────────────────────┐
│ [5A] CORE 0 (Linux, Normal     │   │ [5B] CORE 2 (ARM Cortex-A53       │
│  World)                        │   │  core #2, diverted into Secure   │
│                                 │   │  World the same way Core1 was,   │
│ HARDWARE: mailbox doorbell     │   │  via TF-A's PSCI intercept)      │
│ fires an interrupt (or is      │   │                                  │
│ polled) on Core0                │   │ SOFTWARE: dequeues the frame      │
│                                 │   │ copy Core1 just enqueued          │
│ SOFTWARE: Linux's driver reads │   │                                  │
│ the shared struct, sees a new  │   │ SOFTWARE: can0_compute_hmac() —   │
│ seq value, copies the frame    │   │ recomputes HMAC-SHA256 over       │
│                                 │   │ CAN_ID||counter||payload using    │
│ SOFTWARE: delivers frame to    │   │ the secret key, compares to the   │
│ SocketCAN                       │   │ received tag                     │
│                                 │   │                                  │
│ [Linux now has the frame —     │   │ SOFTWARE: checks counter is       │
│  fast, no crypto wait]          │   │ strictly greater than              │
└───────────────────────────────┘   │ secure_last_counter (replay check)│
                                     │                                  │
                                     │ SOFTWARE: on any failure, appends │
                                     │ to the hash-chained tamper log,   │
                                     │ H_i = HMAC(key, H_(i-1) || entry) │
                                     └──────────────┬───────────────────┘
                                                      │
                                                      ▼
                                     ┌──────────────────────────────────┐
                                     │ [6] POST-FORWARD HOUSEKEEPING      │
                                     │  (back on Core1, happens AFTER    │
                                     │  step 4A/4B — off the critical    │
                                     │  path to Linux)                   │
                                     │                                  │
                                     │ HARDWARE: SPI write — FIFOCON(1)  │
                                     │ UINC bit, tells the chip "this    │
                                     │ message is consumed"              │
                                     │                                  │
                                     │ HARDWARE: SPI write — REG_INT     │
                                     │ cleared, deasserts the chip's     │
                                     │ /INT pin (GPIO25 goes high again) │
                                     │                                  │
                                     │ HARDWARE: write to VC_INTC        │
                                     │ FIQ_CONTROL register — disables   │
                                     │ further FIQ generation, hands the │
                                     │ physical line back for the next   │
                                     │ event                             │
                                     └──────────────────────────────────┘
```

## Step-by-Step: What Happens, and Which Hardware/Software Does It

| Step | What happens | Hardware or software? | Where in the real system |
|---|---|---|---|
| 0 | A CAN-FD frame physically arrives on the bus wire | Hardware — the CAN bus itself | External wire |
| 1 | MCP2518FD chip receives the frame, stores it in its own internal Message RAM, sets its RXIF status bit, asserts its `/INT` output pin | Hardware — the CAN controller chip | MCP2518FD, off-Pi, over SPI0 |
| 2 | The Pi's interrupt controller routes GPIO25's signal as an FIQ instead of a normal Linux IRQ | Hardware — BCM2837's VideoCore interrupt controller, `FIQ_CONTROL` register | On-SoC |
| 3a | Core1 checks the SPI bus is idle before touching it | Software polling a hardware status bit (`SPI_CS.TA`) | `wait_for_spi_idle()`, `rpi3_can_fiq.c` |
| 3b | Core1 reads the FIFO pointer + interrupt-status registers over SPI | Hardware — BCM2835 SPI0 peripheral physically shifting bits | SPI0, between Pi and chip |
| 3c | Core1 reads the actual 60-byte CAN-FD frame object over SPI | Hardware — same SPI0 peripheral | SPI0 |
| 3d | Core1 increments its own ground-truth frame counter | Software — a CPU register/memory write on Core1 | `rpi3_can_fiq.c` |
| 4a | Core1 writes the frame into a shared-DRAM struct, memory-barriers, then writes to Core0's mailbox register | Software write, then a real hardware doorbell register (`ARM_LOCAL_MAILBOX1`) | `rpi3_can_server_push_result()` pattern |
| 4b | Core1 writes a copy of the frame into a separate Secure-World-only struct, memory-barriers, writes to Core2's mailbox register | Same mechanism, different destination core and different (non-Linux-visible) memory region | New Core1→Core2 queue |
| 5a | Linux's driver sees the doorbell, reads the new frame from shared DRAM, delivers it to SocketCAN | Software — Linux kernel driver | Core0, Normal World |
| 5b | Core2 recomputes the HMAC, checks the counter for replay, and logs any mismatch into a hash-chained log | Software — crypto computation on Core2 | Core2, Secure World |
| 6 | Core1 tells the chip "message consumed" (`FIFOCON` UINC) and clears the chip's interrupt flag | Hardware — SPI writes to the physical chip's registers | SPI0, after the Linux handoff is already done |

## Key Point This Diagram Makes Visually

Steps **4A and 4B happen in parallel, both driven directly from what Core1 itself read in step 3** — Linux and Core2 both get their copy from the *same first-hand capture*, not from each other. Step 6 (telling the chip "done") happens **last**, after Linux already has the frame, which is exactly the latency fix the professor asked for.
