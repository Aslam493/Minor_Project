# Raspberry Pi OP-TEE Runtime Verification

**Verified:** 7 September 2026

The corrected image was written to the 8 GB test card and booted physically on
a Raspberry Pi 3. HDMI, keyboard, Ethernet, and SSH were used for validation.

## Corrected image

```text
/home/aslam/Minor_Project/optee_full/out/rpi3-sdcard.img
Size: 336,592,896 bytes
SHA-256: 247ae2d7b899e80df5f191e0d02d1e3565d1e3cd9155e99d61f5185d00924089
```

The kernel was rebuilt with compressed-module decompression enabled:

```text
CONFIG_MODULE_COMPRESS_XZ=y
CONFIG_MODULE_DECOMPRESS=y
CONFIG_XZ_DEC=y
```

Runtime `modprobe can` returned zero and `lsmod` showed the `can` module. No
`Invalid ELF header magic` message remained after reboot.

## OP-TEE runtime

Linux reported OP-TEE revision 4.10 and initialized its driver. Both devices
were present:

```text
/dev/tee0
/dev/teepriv0
```

The system init service automatically ran `tee-supplicant` as the dedicated
`tee` user with `/dev/teepriv0`.

## Test results

The complete `xtest` output is saved in `../Step8-xtest.log`:

```text
36114 subtests of which 0 failed
114 test cases of which 0 failed
0 test cases were skipped
TEE test application done!
