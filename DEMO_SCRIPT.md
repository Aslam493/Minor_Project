# Live Demo Script

A rehearsed, minimal-risk sequence for presenting this project. Practice it
once end-to-end before the real presentation.

## Before you leave for the demo

- [ ] Pi, its power supply, an Ethernet cable (or confirm the demo room's
      Wi-Fi you can join), and the SD card (already verified — checksums
      match, see below) are packed.
- [ ] Laptop's SSH key (`~/.ssh/id_ed25519`) is on the machine you'll demo
      from.
- [ ] **If you won't have network access at the venue**: bring a USB
      keyboard and an HDMI cable so you can log in directly on the Pi
      instead of over SSH (login: `root`, no password). Confirm this
      *before* the day of the demo — everything below assumes SSH, and the
      output looks identical either way, only how you connect changes.
- [ ] Confirm the card is the one that's already verified: on the laptop,
      `sha256sum` the two files below should read (already checked tonight):
      ```
      b8f509657f17c44633fe8d42885ce9e90eef7468b180c09fb69421129e949312  /usr/bin/secure_can_demo
      351476c8fe167570550c13655d93f6070a6fc85dae7dd9ace572b998780eb980  /lib/optee_armtz/abecf637-ca20-467e-8e6c-0b8779ba6b2f.ta
      ```

## Live sequence

1. **Power on the Pi.** Say while it boots (~2–3 min): *"This boots through
   ARM TrustZone's full secure boot chain — Trusted Firmware-A, then OP-TEE
   as the Trusted OS in the secure world, then Linux in the normal world."*

2. **Find its IP and connect.**
   ```sh
   # if you have router/DHCP access, or just watch the Pi's HDMI output
   ssh root@<pi-ip>
   ```
   Note: the IP can change between boots (DHCP) — check before you're at the
   podium, don't assume last time's address.

3. **Show OP-TEE is up** (10 seconds, proves the TrustZone stack is live):
   ```sh
   dmesg | grep -i optee
   ```
   Expect: `optee: revision 4.10 ...`, `optee: initialized driver`.

4. **Run the actual demo:**
   ```sh
   /usr/bin/secure_can_demo
   ```
   Talk through the output as it prints (matches this exactly):
   - `Normal World challenge / Secure World response` — *"Linux sends a
     value into the secure world and gets a transformed value back — proves
     the two worlds can talk."*
   - `Linux (Normal World) starting simulated CAN message pipeline` — *"No
     real CAN hardware yet, so Linux is standing in for a CAN controller —
     it decides when each message 'arrives' and feeds it to the secure
     world. This is a deliberate, documented design choice, not a
     shortcut — see `STEP11_CAN_HARDWARE_SPEC.md`."*
   - `CAN id=0x100/0x101/0x102 classified SECURE ... routing for
     authentication` then a `tag=...` line for each — *"Real CAN buses give
     lower IDs higher priority — that's exactly where safety-critical
     traffic like braking or steering lives. Secure World classifies each
     frame by ID first: low IDs are marked SECURE and get full HMAC-SHA256
     authentication."*
   - `CAN id=0x103/0x104 classified NORMAL ... handled directly in Normal
     World` — *"Higher IDs — lower-priority traffic like telemetry — are
     classified NORMAL and Secure World doesn't spend time authenticating
     them. This is what a real secure gateway does: not every message
     needs the expensive path."*
   - `Linux: simulating receipt of the last SECURE frame, asking Secure
     World to verify it` → `PASS: Secure World accepted the authentic
     frame` — *"This is the receiver side: secure world independently
     re-checks the tag."*
   - `Linux: simulating a tampered frame...` → `PASS: Secure World rejected
     the tampered frame` — **this is the line that matters most.** *"I
     flipped one data byte to simulate a tampered or forged CAN message,
     and the secure world correctly rejected it. This is the actual
     security property: a compromised or malicious normal-world OS cannot
     get a forged message past secure-world verification."*
   - `EXIT_CODE:0` at the very end confirms every check passed.

5. **If asked "why not real CAN hardware yet?"** — point to
   `STEP11_CAN_HARDWARE_SPEC.md`: hardware (MCP2515 module) is specified and
   the exact code change to switch from simulated frames to real
   `SocketCAN` traffic is written out — it's a small, scoped, already-planned
   change, not an open question.

6. **If asked "what was hard about this?"** — `KNOWLEDGE.md` has three real
   incidents with root cause and fix: a secure-world interrupt experiment
   that broke boot and was rolled back, a device-node mixup while flashing,
   and a TA stack-overflow that hung the whole board until the stack size
   was increased. Good material if asked about debugging process, not just
   the end result.

## If something goes wrong live

- **Pi doesn't boot / SSH doesn't connect**: don't improvise fixes on stage.
  `RPI3_BOOT_TROUBLESHOOTING.md` has the full diagnostic table. If you can't
  resolve it in under a minute, move to narrating what *should* happen using
  this script and the logs already captured (`Step10-runtime.log`,
  `Step8-xtest.log`) as evidence it has worked.
- **Command hangs**: `Ctrl+C`, wait a few seconds, retry once. If the whole
  board stops responding (has happened once before, see `KNOWLEDGE.md`
  Incident 3), it needs a power cycle — don't keep retrying commands against
  a hung board on stage; power-cycle immediately and re-run from step 1.
