# Step 7 Completion Record

**Completed:** 6 September 2026

Step 7 is complete on the 8 GB test SD card. The main 32 GB project card was
not modified and remains available as the fallback card.

## Image generation

The supported RPi3 image script generated:

```text
/home/aslam/Minor_Project/optee_full/out/rpi3-sdcard.img
Size: 336,592,896 bytes (321 MiB)
SHA-256: 39fa3171b447234bf232a5c7cf36fc14e53e6e00ccd2d8dbb260ea2d04b61b58
```

The first attempt stopped because `mcopy` was unavailable. After installing
Ubuntu's `mtools` package, the image script completed successfully. The full
diagnostic and successful retry output is preserved in `Step7-image-build.log`.

The image has an MBR partition table:

| Partition | Start sector | Size | Type |
| --- | ---: | ---: | --- |
| 1 | 2,048 | 63 MiB | W95 FAT16 (LBA), bootable |
| 2 | 133,120 | 256 MiB | Linux ext4 |

The root partition extracted logically from the image matched
`out-br/images/rootfs.ext2` exactly by SHA-256.

## Target-card safety record

Before writing, the removable target was identified separately from the laptop
system disk:

```text
Target test card: /dev/sda, 7,730,102,272 bytes, removable USB
Laptop root disk: /dev/nvme0n1p6
```

The user explicitly authorized writing `/dev/sda`. Both existing partitions
were unmounted before the image was written. `dd` reported:

```text
80+1 records in
80+1 records out
336592896 bytes copied
```

The write was flushed with `sync`.

## Post-write verification

The written card exposed the expected filesystems:

```text
/dev/sda1: 63 MiB FAT
/dev/sda2: 256 MiB ext4, label rootfs
```

Both partitions were mounted read-only. All 17 boot files were compared with
the assembled source files by SHA-256; the mismatch count was zero.

The SD-card root filesystem contains:

- ARM64 `tee-supplicant`, `xtest`, and `libteec`;
- 11 OP-TEE example host applications;
- 36 Trusted Applications;
- modules for Linux `6.7.0-rc6-v8`; and
- nine relevant SocketCAN/MCP251x module files.

Finally, both partitions were cleanly unmounted and `/dev/sda` was powered off
