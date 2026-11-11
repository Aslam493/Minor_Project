# N-04: Linux does not reserve OP-TEE's secure memory

Date: 2026-11-06. Status: **RESOLVED 2026-11-12: candidate installed on the 8 GB spare card and verified on the Pi (section 12); durable build rule added.** (Original text below describes the state of 2026-11-06.)
All Pi-side steps were read-only (one temporary read-only mount of the boot partition, released each time).

## 1. The problem, with the evidence

| Fact | Value | Evidence |
|---|---|---|
| OP-TEE secure DRAM | `0x10100000-0x10FFFFFF` (15 MiB) | `out/arm/conf.mk`, compiled `conf.h`, `tee-header_v2.bin` (load address), TF-A `SEC_DRAM0_BASE/SIZE` with `RPI3_USE_UEFI_MAP := 0`; no override in `build/rpi3.mk` |
| TF-A secure SRAM | `0x10000000-0x100FFFFF` (1 MiB) | TF-A `SEC_SRAM_BASE/SIZE` |
| Shared memory (OP-TEE) | `0x08000000-0x083FFFFF` (4 MiB) | `conf.mk` |
| What Linux reserves | `0x08000000-0x09FFFFFF` (32 MiB), CMA 64 MiB, a few boot-time ranges | live `/proc/iomem`, live DTB `/memreserve/` |
| Linux's RAM | one bank `0x00000000-0x3B3FFFFF` | live `/proc/device-tree/memory@0/reg`, `/proc/iomem` |
| Pages in `0x10000000-0x10FFFFFF` (4,096) | **100% free (BUDDY)** in Linux's per-page flags | `/proc/kpageflags` |
| Pages in `0x08000000-0x09FFFFFF` (8,192) | 100% RESERVED | `/proc/kpageflags` |

So Linux's allocator considers OP-TEE's and TF-A's memory to be ordinary free RAM. It has not used those pages so far (Linux uses about 6 percent of its RAM); that is luck of allocation order. **No corruption has been observed or demonstrated.** What exists is a configuration gap.

## 2. How Linux's device tree is made

```
kernel source  linux/arch/arm/boot/dts/broadcom/bcm2710-rpi-3-b.dts   line 3: /memreserve/ 0x08000000 0x02000000
   (committed in the kernel tree by "8bfce8398 rpi3: Enable PSCI support"; written for an older memory layout)
        -> built -> bcm2710-rpi-3-b.dtb  -> build/rpi3.mk update_bootfs: fdtoverlay + CAN-shield overlay
        -> boot partition file bcm2710-rpi-3-b.dtb  (34,642 bytes, sha256 6b90c3e0...0d3826)
U-Boot (uboot.env): select_fdt picks bcm2710-rpi-3-b.dtb for board "3 Model B"; fatload mmc 0:1 to 0x01000000; booti kernel - dtb
U-Boot fixups before booti -> the tree Linux gets (36,864 bytes)
```

Exactly what U-Boot changes (decompiled diff of the file against the live tree, 23 lines): `serial-number`, a `framebuffer@3e513000` node, `chosen/bootargs` (the full kernel command line), `local-mac-address`, and the `/memory` node size (`0` in the file, `0x3b400000` in the live tree). **It adds no reservation, and the `/memreserve/` tables are identical in the file and in the live tree.** TF-A's own reservation code edits a different tree (the one at `0x00010000`, which Linux never uses, Incident 5) and only reserves the 1 MiB SRAM.

Consequence: **a change to the single file `bcm2710-rpi-3-b.dtb` on the boot partition is enough; U-Boot will not undo it.**

## 3. The reservation that is needed

`0x10000000 .. 0x10FFFFFF` (16 MiB): TF-A secure SRAM plus OP-TEE secure DRAM, matching TF-A and OP-TEE. The existing 32 MiB reservation at `0x08000000` must stay (it covers the shared memory).

## 4. Options

| | Option | How | Good | Bad |
|---|---|---|---|---|
| **A** | Edit the kernel's DTS and rebuild the device tree | change line 3 or add a node in the source, rebuild | clean source of truth | the build runs `update_bootfs`, which depends on `tf-a`; the TF-A and OP-TEE working trees are modified (Core 1 divert), so a rebuild must be handled carefully (Incident 5); touches the kernel tree |
| **B** | **Add the node with `fdtoverlay` to the existing boot-partition DTB (recommended)** | one small overlay applied with the tool the build already uses for the CAN shield | smallest change (+71 bytes, one node); touches no firmware source; reversible by copying one file | the persistent fix still needs one line in `build/rpi3.mk` so that the next image build does not lose it |
| C | U-Boot environment: `fdt addr`, `fdt rsvmem add` before `booti` | edit `uboot.env` | no DTB change | changes boot logic, not data; a mistake leaves U-Boot at a prompt with no console; the file is a binary with a CRC |
| D | Limit Linux's RAM with `mem=` | boot arguments in the environment | trivial | throws away about 690 MB of RAM; still an environment edit |

`no-map` (used in B) goes beyond a plain `/memreserve/`: Linux also drops the region from its own kernel mapping, so a stray kernel access faults instead of silently writing.

## 5. The candidate (prepared offline, **not installed**)

Files in `Part_E_Verification/monitor/n04/`: `n04-optee-reserve.dts` (source), `n04-optee-reserve.dtbo`, `original-bcm2710-rpi-3-b.dtb`, `candidate-bcm2710-rpi-3-b.dtb`, `SHA256SUMS`.

| Check | Result |
|---|---|
| Overlay applies with `fdtoverlay` | yes |
| Decompiled difference between original and candidate | **exactly one added node**, 6 lines: `optee_secure@10000000 { no-map; reg = <0x10000000 0x1000000>; }` under `/reserved-memory` |
| `/memreserve/` table | unchanged (`0x08000000+32 MiB`, `0x0+4 KiB`) |
| Header sanity | magic `0xd00dfeed`, `totalsize` equals the file size (34,713), version 17 |
| `reg_format` warnings on the merged tree | 0 (the warning printed when compiling the overlay alone is dtc assuming 2-cell addressing; the real parent `/reserved-memory` is 1-cell, so the 8-byte `reg` is correct) |
| Install and restore rehearsal | on a scratch copy of the Sep-20 image: install, read back (`e8ac38fa...`), restore, read back (`6b90c3e0...`); **all 17 boot-partition files have their original content afterwards** |
| Can it be booted offline? | **no**: there is no QEMU here, and `dtc`/`fdtoverlay` only prove the file is well-formed, not that the kernel and U-Boot accept it. The real test is the Pi |

## 6. Preconditions you listed

| Precondition | Status |
|---|---|
| Know the exact DTB used | **yes**: `bcm2710-rpi-3-b.dtb` from the FAT partition, selected by `select_fdt` |
| Know the exact reservation required | **yes**: `0x10000000-0x10FFFFFF`, 16 MiB |
| A verified copy of the original DTB | **yes**: `SD_Card_Backups/pre-S0-sep20/boot-files/bcm2710-rpi-3-b.dtb` (+ `.sha256`), identical to the file on the Pi's card, to the one inside the Sep-20 backup image, and to `n04/original-...` (`6b90c3e0939b0a18222c78700ffcac427253e6ca950aa277cee96429cc0d3826`) |
| A recovery procedure for a single card | **written (section 7) and its mechanics rehearsed offline**; the real card has not been used for it |

## 7. Install and recovery procedure for a single card (DRAFT, not executed)

Before anything: your explicit approval; a USB microSD reader at hand; the laptop password (the passwordless rule does not cover this).

1. **Install, on the running Pi:** copy `candidate-bcm2710-rpi-3-b.dtb` to `/tmp`; check its sha256 on the Pi; mount the boot partition read-write at `/tmp/bp`; copy the file over `bcm2710-rpi-3-b.dtb`; `sync`; read back and compare the hash; unmount. Do not power-cycle during this. Nothing else on the partition is touched.
2. **Verify after reboot:** `verify_boot.sh --expect-core1-pinned` (18 checks) **plus** three N-04 checks: `/proc/iomem` shows `10000000-10ffffff : reserved`; `/proc/kpageflags` shows 4,096 RESERVED pages over that range; `/proc/device-tree/reserved-memory/optee_secure@10000000` exists. Also `secure_can_demo` still passes.
3. **If the Pi does not come back** (no SSH within 120 s): power it off; take out the card; put it in the reader; identify the device with `lsblk` and `file /dev/sdX`; restore the one file: `sudo mcopy -o -i /dev/sdX1 SD_Card_Backups/pre-S0-sep20/boot-files/bcm2710-rpi-3-b.dtb ::bcm2710-rpi-3-b.dtb`; verify its hash; reinsert and boot.
4. **If that does not help:** restore the whole card from a verified backup image (`ROLLBACK_PROCEDURE.md`). With one card the "never overwrite the original" rule cannot be kept; the verified backups are the only safety net, and they exist only on this laptop (copy them off first).

Risk statement: a device-tree mistake can hang boot (Incident 5) and there is no UART to see why. The change is small and well-formed, its install and restore mechanics were rehearsed, but it has never been booted. **Risk is low but not zero.**

## 8. What the fix does and does not do

- It stops **Linux's allocator** (and, with `no-map`, Linux's own kernel mapping) from using TF-A's and OP-TEE's memory, so monitor state cannot be overwritten by a Linux allocation. Linux loses 16 MiB (1.7 percent of its RAM); there is no live data to move because every page there is free now.
- It does **not** stop a deliberate write by a compromised kernel, a kernel module, or a DMA-capable device: the Pi 3 has no TZASC. The report must still not claim hardware-backed isolation.
- It does not fix the persistent build: the next `update_bootfs` would regenerate the old device tree. The durable fix is one more `fdtoverlay` line in `build/rpi3.mk` (a source change, needs approval).
- The B+ device tree is not touched (this board is a Model B).

## 9. Not established

That the kernel and U-Boot accept the candidate (never booted); that OP-TEE physically sits at `0x10100000` (derived from four agreeing configuration facts, not read: `/dev/mem` refuses RAM and there is no UART); the cost of a pass-through of all 16 MiB if some other firmware component also uses memory in that range.

## 10. Side findings

- The FAT filesystem on every image claims 131,072 sectors but its partition holds 129,024 (2,048 short). `fsck.fat` reports `Failed to read sector 131071` on the **original** backup too, so it is not caused by anything done here. Harmless while files stay at the start of the volume; worth fixing in the image builder.
- An earlier statement in 1.3 (secure RAM at `0x08400000`) was wrong and has been corrected in `COMMUNICATION_BASELINE.md`.

## 11. Problems or confusion avoided

| Item | What happened | How it was handled |
|---|---|---|
| `find /proc/device-tree` printed nothing | `/proc/device-tree` is a symlink; `find` does not follow it without a trailing slash | reran with the slash |
| A warning while compiling the overlay | `dtc` assumed 2-cell addressing for a standalone overlay | checked the merged tree: 0 warnings, and the parent has 1-cell addressing |
| `fsck.fat` errors after the rehearsal | looked like damage | ran it on the untouched original: same message, so it pre-exists (a FAT/partition size mismatch) |
| Whether a one-file change is enough | U-Boot rewrites several things | diffed the files: it rewrites five items, none of them a reservation |

## 12. Installed and verified on hardware (2026-11-12, user approved: 'Install now, I'm at the Pi')
- Preflight: Pi's boot-partition DTB sha256 = `6b90c3e0...0d3826` (the original); live `/proc/iomem` showed no reservation of 0x10000000-0x10ffffff. Copy of the Pi's file saved as `n04/pi_dtb_before_install_20261112.dtb` (same hash). Candidate checked against `n04/SHA256SUMS` and re-hashed on the Pi (`e8ac38fa...744ccd`) before writing.
- Install (SSH, 8 GB spare only; 32 GB original card not touched): boot partition remounted rw, one file copied, `sync`, read back `e8ac38fa...` (34,713 B), unmounted; `armstub8.bin` (`775ce384...`) unchanged. Then reboot; Pi back on SSH in ~45 s.
- After reboot: `verify_boot.sh` **16 pass / 0 FAIL / 2 skipped** (same as before); `/proc/iomem` **`10000000-10ffffff : reserved`** and System RAM now starts at `11000000`; `/proc/device-tree/reserved-memory/optee_secure@10000000` exists with `reg = 0x10000000 0x01000000` and `no-map`; kernel log: `OF: reserved mem: 0x10000000..0x10ffffff (16384 KiB) nomap non-reusable optee_secure@10000000`; no oops/BUG; MemTotal 880880 kB. Monitor after reboot: `init_ok=1 starved=0 skip_count=0 pta_lock_timeouts=0 ev_lost=0`, Core 1 polling (heartbeat hb_seq rising, no responder so misses accumulate as before).
- Durable fix: `optee_full/build/rpi3.mk` (`RPI3_OPTEE_RESERVE_DTBO`, third argument of the Model B `fdtoverlay`), files `br-ext/board/rpi3/optee-reserve.{dts,dtbo}`. Offline reproduction with the build's own tools (`linux/scripts/dtc`): raw `bcm2710-rpi-3-b.dtb` + Seeed overlay + `optee-reserve.dtbo` in one `fdtoverlay` call = 34,713 B, **0 differing lines** (sorted decompile) vs the DTB running on the Pi; the `.dtbo` rebuilt from the `.dts` is byte-identical to the committed one. **The full image build (`update_bootfs`) has NOT been run**; the rule is verified by reproducing its command, not by a build.
- Not changed by this fix: no TZASC on the Pi 3, so a deliberate write by a compromised kernel or DMA device is still possible; this only stops Linux's allocator and kernel mapping from using the range. kpageflags page count was not re-read (the /proc/iomem and kernel log evidence is direct).
- Rollback if ever needed: copy `SD_Card_Backups/pre-S0-sep20/boot-files/bcm2710-rpi-3-b.dtb` (or `n04/pi_dtb_before_install_20261112.dtb`) back over the boot-partition file, hash `6b90c3e0...`.
