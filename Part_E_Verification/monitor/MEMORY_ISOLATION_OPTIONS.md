# Normal World access to Secure World memory: options (stored 2026-11-18, to be discussed later)

**Question (user):** can the Normal World be given no write and no execute (read at most) on Secure World memory, so it cannot "enter" Secure World?

**Answer given:** that is what a memory-protection controller (TZASC / TZC-400) does, but the Raspberry Pi 3 (BCM2837) has none, so it cannot be enforced in hardware here. Today Normal World reaches Secure World only through the defined call interface; that is design intent, not hardware enforcement. Root on the Pi could in principle address the secure RAM `0x10000000-0x10ffffff` (reserved from the kernel by N-04, which is bookkeeping, not protection).

| Option | Possible on the Pi 3? | Strength |
|---|---|---|
| Hardware block by a TrustZone memory controller | No (no such controller) | Strong, not available |
| Small hypervisor at EL2 with stage-2 page tables (read / write / execute rights for Linux) | In theory; large new software; conflicts with the "no TF-A source change" rule; no IOMMU, so GPU and DMA still reach RAM | Medium |
| Linux-side hardening (no `/dev/mem`, kernel lockdown, no module loading, root-only) | Partly; some items need a kernel rebuild | Weak: relies on the Linux we do not trust; helps only against an attacker with userspace root |
| Core 1 checks its own state for tampering | Possible | Detection only; forgeable by an attacker who reads the key |

**Facts measured on the Pi (read-only, 2026-11-18):** `/dev/mem` exists and is root-only (`crw-------`); `/proc/iomem` lists `10000000-10ffffff : reserved`. **Not tested:** whether root can actually read or write that range through `/dev/mem`. OP-TEE's hardware unique key is all zeros on this platform (`CFG_INSECURE` fallback in `otp_stubs.c`).

**Recommended position for the report:** keep "detection only, no prevention; the Pi 3 has no memory isolation"; name the real fix (a SoC with a memory protection controller and a key in secure storage).

**Open for the user to decide later:** (a) do the safe measurement (can `/dev/mem` read the reserved range, without printing key bytes) and one or two cheap hardening steps that need no flashing, reported as "reduces risk, not isolation"; or (b) only update the report text.
