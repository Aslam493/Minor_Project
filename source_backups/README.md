# Source backups for changes living in gitignored/nested repos

`optee_full/` is gitignored by this repo, and both `optee_full/optee_os`
and `optee_full/trusted-firmware-a` are themselves separate git repos
whose only remotes are their respective public upstream projects —
project-specific work must never be pushed there. This directory holds
portable backups of such work so it's recoverable from this repo alone,
without depending on any particular machine's local checkout.

## optee_os-secure-peripheral-ownership.bundle

Contains both Secure Peripheral Ownership phases (KNOWLEDGE.md's
seventh and eighth addenda): Phase 1 (the `rpi3_can_spi_poc` pseudo-TA,
reading the CAN chip directly over SPI0) and Phase 2 (`rpi3_can_fiq.c`,
routing the CAN chip's interrupt to Secure World directly).

**To restore**, from a fresh clone of upstream OP-TEE OS
(`https://github.com/OP-TEE/optee_os.git`) pinned at commit
`d5001fe0af66a4c059dc6816a7be08d7fb83ff1a` (the exact commit this
project's `optee_full/optee_os` was checked out at when this backup was
made):

```sh
git clone https://github.com/OP-TEE/optee_os.git
cd optee_os
git checkout d5001fe0af66a4c059dc6816a7be08d7fb83ff1a
git bundle unbundle /path/to/optee_os-secure-peripheral-ownership.bundle
git checkout secure-can-spi-poc
```

This recreates the exact working tree that was built and verified
working on hardware: real SPI0 read of the MCP2518FD's Oscillator
Configuration Register (`0x00000468`), and the CAN controller's
interrupt reaching Secure World directly (FIQ count 1, zero regression
to the existing pipeline).

## trusted-firmware-a-can-fiq-routing.bundle

Contains the one-function fix needed for Phase 2 above:
`plat_ic_get_pending_interrupt_type()` in
`plat/rpi/common/rpi3_common.c`, previously a hard-coded stub that
errored on any interrupt reaching EL3.

**To restore**, from a fresh clone of upstream TF-A
(`https://git.trustedfirmware.org/TF-A/trusted-firmware-a.git`) pinned
at commit `a1f02f4f3daae7e21ee58b4c93ec3e46b8f28d15`:

```sh
git clone https://git.trustedfirmware.org/TF-A/trusted-firmware-a.git
cd trusted-firmware-a
git checkout a1f02f4f3daae7e21ee58b4c93ec3e46b8f28d15
git bundle unbundle /path/to/trusted-firmware-a-can-fiq-routing.bundle
git checkout secure-can-fiq-routing
```

## Restoring into this project's existing checkouts

If restoring into `optee_full/optee_os` or `optee_full/trusted-firmware-a`
directly (rather than a fresh clone), skip the `git clone`/`checkout
<pinned commit>` steps — just run `git bundle unbundle` and `git
checkout` from inside the existing checkout, provided it's still at the
same pinned commit (these checkouts normally aren't moved off their
pinned commit day-to-day).
