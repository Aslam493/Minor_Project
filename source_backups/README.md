# Source backups for changes living in gitignored/nested repos

`optee_full/` is gitignored by this repo, and `optee_full/optee_os` is
itself a separate git repo whose only remote is the public upstream
OP-TEE project (`https://github.com/OP-TEE/optee_os.git`) — project-
specific work must never be pushed there. This directory holds portable
backups of such work so it's recoverable from this repo alone, without
depending on any particular machine's local checkout.

## optee_os-secure-peripheral-ownership.bundle

Contains the Phase 1 Secure Peripheral Ownership commit (KNOWLEDGE.md's
seventh addendum): the `rpi3_can_spi_poc` pseudo-TA and the
`plat-rpi3` platform changes it needs.

**To restore**, from a fresh clone of upstream OP-TEE OS pinned at
commit `d5001fe0af66a4c059dc6816a7be08d7fb83ff1a` (the exact commit this
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
working on hardware (real SPI0 read of the MCP2518FD's Oscillator
Configuration Register, value `0x00000468`).

If restoring into this project's actual `optee_full/optee_os` checkout
instead of a fresh clone, skip the `git clone`/`checkout <pinned commit>`
steps — just run `git bundle unbundle` and `git checkout` from inside
the existing checkout, provided it's still at the same pinned commit
(`optee_full/optee_os` normally isn't moved off that commit day-to-day).
