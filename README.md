# Secure Automotive Event Logging with OP-TEE on Raspberry Pi 3

This Minor Project demonstrates a secure CAN-message processing design using
ARM TrustZone, OP-TEE, Trusted Firmware-A, U-Boot, Linux, and Buildroot on a
Raspberry Pi 3 Model B or Model B+.

## Current status

Steps 1 through 6 are complete:

| Step | Work | Status |
| --- | --- | --- |
| 1 | Build OP-TEE OS | Complete |
| 2 | Build Trusted Firmware-A and U-Boot | Complete |
| 3 | Build ARM64 Linux and Raspberry Pi DTBs | Complete |
| 4 | Select and verify the authoritative source tree | Complete |
| 5 | Build the root filesystem and OP-TEE user space | Complete |
| 6 | Assemble the Raspberry Pi boot files | Complete |
| 7 | Generate and write the SD-card image | Not started |
| 8 | Capture the first serial boot | Not started |

The detailed implementation plan is in
[`RPI3_SECURE_CAN_FULL_ROADMAP.md`](RPI3_SECURE_CAN_FULL_ROADMAP.md).

## Intended secure packet flow

```text
Sender ECU
    ↓
CAN bus
    ↓
External CAN controller and transceiver
    ↓
Secure-designated GPIO interrupt
    ↓
OP-TEE secure interrupt handler
    ↓
Secure SPI/CAN receive driver
    ↓
Secure RAM
    ↓
CAN-ID, HMAC, counter, replay, and flood checks
    ↓
Critical packet → secure log
Normal packet   → approved shared memory → Linux
```

This Raspberry Pi implementation is an educational prototype. It can
demonstrate TrustZone execution and the approved packet flow, but it must not be
described as production-grade automotive hardware isolation.

## What was built

The authoritative local build tree is:

```text
/home/aslam/Minor_Project/optee_full
```

Important completed outputs include:

```text
OP-TEE OS
└── optee_full/optee_os/out/arm/core/tee.bin

Trusted Firmware-A
└── optee_full/trusted-firmware-a/build/rpi3/debug/armstub8.bin

U-Boot
└── optee_full/u-boot/u-boot.bin

Linux
├── optee_full/linux/arch/arm64/boot/Image
├── bcm2710-rpi-3-b.dtb
└── bcm2710-rpi-3-b-plus.dtb

Buildroot
├── optee_full/out-br/images/rootfs.cpio.gz
└── optee_full/out-br/images/rootfs.ext2

Assembled boot directory
└── optee_full/out/boot/
```

The Linux and module release is `6.7.0-rc6-v8`. OP-TEE support is built into
the kernel with `CONFIG_TEE=y` and `CONFIG_OPTEE=y`.

## OP-TEE user space

The root filesystem contains:

- `tee-supplicant`;
- `xtest`;
- `libteec` and `libckteec`;
- 11 OP-TEE example host applications;
- all 11 matching example Trusted Applications; and
- 36 Trusted Applications in total.

Cross-architecture ELF inspection found zero missing shared-library
dependencies.

## Raspberry Pi boot directory

The complete boot directory committed to this repository is
[`optee_full/out/boot`](optee_full/out/boot). It contains:

- Raspberry Pi firmware (`bootcode.bin`, `start*.elf`, and `fixup*.dat`);
- `config.txt`;
- the combined TF-A/OP-TEE boot chain in `armstub8.bin`;
- ARM64 Linux as `kernel8.img`;
- DTBs for Raspberry Pi 3 Model B and Model B+; and
- the compiled U-Boot environment in `uboot.env`.

`armstub8.bin` already packages the TF-A FIP containing BL2, BL31, OP-TEE as
BL32, and U-Boot as BL33. Do not copy `fip.bin` separately to the boot
partition.

## Verification records

| Record | Purpose |
| --- | --- |
| [`Step4_Verification/STEP4_COMPLETION.md`](Step4_Verification/STEP4_COMPLETION.md) | Authoritative tree and verified earlier builds |
| [`Step5_Verification/STEP5_COMPLETION.md`](Step5_Verification/STEP5_COMPLETION.md) | Rootfs and OP-TEE user-space verification |
| [`Step5_Verification/SHA256SUMS`](Step5_Verification/SHA256SUMS) | Step 5 artifact hashes |
| [`Step6_Verification/STEP6_COMPLETION.md`](Step6_Verification/STEP6_COMPLETION.md) | Boot assembly verification |
| [`Step6_Verification/SHA256SUMS`](Step6_Verification/SHA256SUMS) | Boot and rootfs hashes |
| [`COMPONENT_REVISIONS.md`](COMPONENT_REVISIONS.md) | Exact upstream Git revisions |
| [`GITHUB_BACKUP.md`](GITHUB_BACKUP.md) | What is and is not stored on GitHub |

From the repository root, verify the files available in a GitHub clone with:

```sh
sha256sum -c Step6_Verification/SHA256SUMS --ignore-missing
```

`rootfs.ext2` is 256 MB and exceeds GitHub's per-file limit, so it is not in
the repository. Its checksum is recorded, and the repository includes the
compressed 34 MB `rootfs.cpio.gz` recovery artifact.

## Reproducing the build

The full source checkouts and cross-toolchains are not committed because the
local build tree is about 20 GB. Recreate the OP-TEE checkout using the exact
revisions in [`COMPONENT_REVISIONS.md`](COMPONENT_REVISIONS.md), install
x86_64-hosted AArch64/AArch32 cross-toolchains, and then use:

```sh
cd /home/aslam/Minor_Project/optee_full
make -j4 -C build \
  PYTHON3=/home/aslam/Minor_Project/optee-build-venv/bin/python \
  buildroot

make -j1 -C build \
  PYTHON3=/home/aslam/Minor_Project/optee-build-venv/bin/python \
  update_bootfs
```

## Next step: SD-card image

Step 7 will generate `optee_full/out/rpi3-sdcard.img`, identify the SD card
using read-only commands, confirm its exact device name and capacity, and only
then write the image.

Do not copy and run a `dd` command with a guessed device such as `/dev/sdb`.
Writing to the wrong device can destroy the laptop's data. The SD-card write
must wait until the actual removable device has been identified and confirmed.
