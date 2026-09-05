# OP-TEE Structure and How It Works on Raspberry Pi 3

## Current status

OP-TEE is currently **compiled and packaged into the TF-A firmware**, but the complete system has not yet been installed on a bootable OP-TEE SD-card image or proven running on the Raspberry Pi.

## Project structure on the Raspberry Pi

```text
~/Minor/optee_project/
├── optee_os/               Secure-world operating system
│   └── out/arm/core/
│       ├── tee.elf
│       ├── tee.bin
│       ├── tee-header_v2.bin
│       ├── tee-pager_v2.bin
│       └── tee-pageable_v2.bin
│
├── trusted-firmware-a/     EL3 secure monitor and early firmware
│   └── build/rpi3/debug/
│       ├── fip.bin
│       └── armstub8.bin
│
├── u-boot/
│   └── u-boot.bin
│
├── linux/
│   ├── .config
│   └── arch/arm64/boot/
│       ├── Image
│       └── dts/broadcom/
│           ├── bcm2710-rpi-3-b.dtb
│           └── bcm2710-rpi-3-b-plus.dtb
│
├── optee_client/           Normal-world OP-TEE library and daemon
├── optee_examples/         Example client apps and Trusted Apps
├── optee_test/             OP-TEE test suite
└── build/                  Connects and packages the components
```

## OP-TEE build outputs

The secure-world source is in:

```text
optee_os/
```

It generated:

```text
optee_os/out/arm/core/tee.bin
optee_os/out/arm/core/tee.elf
optee_os/out/arm/core/tee-header_v2.bin
optee_os/out/arm/core/tee-pager_v2.bin
optee_os/out/arm/core/tee-pageable_v2.bin
```

TF-A receives the split OP-TEE files as BL32:

```text
BL32        = tee-header_v2.bin
BL32_EXTRA1 = tee-pager_v2.bin
BL32_EXTRA2 = tee-pageable_v2.bin
```

U-Boot is passed to TF-A as BL33:

```text
BL33 = u-boot.bin
```

TF-A then creates the Firmware Image Package:

```text
fip.bin
├── TF-A firmware stages
├── BL32: OP-TEE OS
└── BL33: U-Boot
```

For Raspberry Pi 3, TF-A combines padded BL1 and the FIP:

```text
armstub8.bin = padded BL1 + fip.bin
```

Therefore, OP-TEE is packaged inside:

```text
trusted-firmware-a/build/rpi3/debug/armstub8.bin
```

## Intended boot sequence

```text
Raspberry Pi Boot ROM
        ↓
VideoCore firmware reads the boot partition
        ↓
armstub8.bin loaded at address 0x00000000
        ↓
TF-A BL1 and later TF-A stages
        ↓
TF-A loads the FIP
        ├── BL32 → OP-TEE secure world
        └── BL33 → U-Boot normal world
                         ↓
              U-Boot loads kernel8.img
                         ↓
                   Linux starts
```

The TrustZone separation is:

```text
                     ARM processor
                           │
             ┌─────────────┴─────────────┐
             │                           │
        Secure world                Normal world
             │                           │
         OP-TEE OS                    U-Boot
             │                           ↓
      Trusted Applications             Linux
                                         │
                                OP-TEE Linux driver
```

## Linux communication with OP-TEE

Linux was built with:

```text
CONFIG_TEE=y
CONFIG_OPTEE=y
```

The communication path is:

```text
Normal Linux application
        ↓
OP-TEE Client API
        ↓
libteec
        ↓
/dev/tee0
        ↓
Linux OP-TEE driver
        ↓
Secure Monitor Call (SMC)
        ↓
TF-A secure monitor
        ↓
OP-TEE OS
        ↓
Trusted Application
```

Component roles:

- `optee_client` provides normal-world components such as `libteec` and `tee-supplicant`.
- `optee_examples` provides example client applications and Trusted Applications.
- `optee_test` provides `xtest` for testing communication with OP-TEE.
- `CONFIG_TEE=y` enables the generic Linux TEE subsystem.
- `CONFIG_OPTEE=y` enables the Linux OP-TEE driver.
- OP-TEE OS runs Trusted Applications in the secure world.

## Completed work

```text
OP-TEE source present                 ✅
OP-TEE secure-world binaries built    ✅
OP-TEE configured as TF-A BL32        ✅
U-Boot configured as TF-A BL33        ✅
fip.bin generated                     ✅
armstub8.bin generated                ✅
Linux TEE framework enabled           ✅
Linux OP-TEE driver enabled           ✅
Linux Image and Pi 3 DTBs built       ✅
```

## Work still remaining

```text
Final boot directory assembled         ❌
Buildroot root filesystem completed     ❌
Bootable rpi3-sdcard.img generated      ❌
Image written to target SD card         ❌
TF-A and OP-TEE boot observed           ❌
/dev/tee0 verified in Linux             ❌
tee-supplicant verified                 ❌
xtest executed                          ❌
Trusted Application tested              ❌
```

## Correct conclusion

> OP-TEE OS has been compiled and packaged as TF-A's BL32 secure payload inside `armstub8.bin`. Linux has the TEE framework and OP-TEE driver enabled. However, the complete boot image has not yet been assembled, installed and boot-tested on the Raspberry Pi.

## Important project limitation

The TF-A Raspberry Pi 3 port is intended as a prototype and learning platform. Its secure and non-secure memory areas are located in ordinary DRAM, so it must not be presented as providing production-grade physical secure-memory isolation.
