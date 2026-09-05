# Step 6 Completion Record

**Completed:** 6 September 2026

Step 6 is complete. The supported RPi3 `update_bootfs` target assembled the
boot partition at `/home/aslam/Minor_Project/optee_full/out/boot`.

## Build result

The first attempt exposed a cached ARM64-hosted TF-A `fiptool`, which could not
run on the x86_64 laptop. Only that generated host utility was cleaned and
rebuilt with the laptop's `gcc`. Its architecture was verified as x86-64, after
which TF-A rebuilt `fip.bin` and the combined `armstub8.bin` successfully. The
resumed `update_bootfs` target completed and copied all boot files.

The diagnostic and successful retry output is preserved in `Step6-build.log`.

## Assembled boot files

| File | Size (bytes) | Purpose |
| --- | ---: | --- |
| `armstub8.bin` | 1,278,984 | Combined TF-A boot chain including the FIP |
| `kernel8.img` | 20,371,968 | ARM64 Linux kernel Image |
| `bcm2710-rpi-3-b.dtb` | 33,616 | Raspberry Pi 3 Model B device tree |
| `bcm2710-rpi-3-b-plus.dtb` | 34,235 | Raspberry Pi 3 Model B+ device tree |
| `config.txt` | 71 | Raspberry Pi firmware configuration |
| `uboot.env` | 16,384 | Compiled U-Boot environment |
| `bootcode.bin` | 52,476 | Raspberry Pi boot firmware |
| `start.elf` | 2,975,680 | Raspberry Pi GPU firmware |
| `start_cd.elf` | 805,756 | Cut-down GPU firmware |
| `start_db.elf` | 4,819,624 | Debug GPU firmware |
| `start_x.elf` | 3,722,504 | Camera/codec GPU firmware |
| `fixup.dat` | 7,266 | Firmware memory fixup data |
| `fixup_cd.dat` | 3,173 | Cut-down fixup data |
| `fixup_db.dat` | 10,230 | Debug fixup data |
| `fixup_x.dat` | 10,228 | Camera/codec fixup data |
| `COPYING.linux` | 18,693 | Linux firmware licence |
| `LICENCE.broadcom` | 1,594 | Broadcom firmware licence |

## Content verification

Byte-for-byte comparisons passed for:

- Linux `Image` to `out/boot/kernel8.img`
- TF-A `armstub8.bin` to `out/boot/armstub8.bin`
- both Linux RPi3 DTBs to their boot-directory copies
- the RPi3 `config.txt` source to its boot-directory copy

`file` recognized `kernel8.img` as an ARM64 Linux kernel boot executable and
both DTBs as valid version-17 Device Tree Blobs.

TF-A `fiptool info` confirmed that the FIP embedded in the boot chain contains
BL2, BL31, the OP-TEE BL32 payload and its extra image, and U-Boot as BL33. A
separate `fip.bin` is therefore not copied to the boot partition.

The assembled configuration is:

```text
enable_uart=1
kernel_address=0x02000000
device_tree_address=0x01000000
```

The Step 5 root filesystems remain present, including `rootfs.cpio.gz`,
`rootfs.ext2`, and the `rootfs.ext4` link.

## Result

- [x] Supported boot assembly target completed.
- [x] Raspberry Pi firmware files and licences are present.
- [x] `config.txt`, combined `armstub8.bin`, and U-Boot environment are present.
- [x] ARM64 kernel and both Raspberry Pi 3 DTBs are present and match sources.
- [x] Root filesystem artifacts remain complete.
- [x] SHA-256 checksums are recorded and verified in `SHA256SUMS`.

This is assembly-time evidence. Writing an SD-card image is intentionally left
