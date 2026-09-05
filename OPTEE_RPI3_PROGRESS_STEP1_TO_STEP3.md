# Minor Project: Raspberry Pi 3 + OP-TEE

## Detailed progress notes: Step 1 through Step 3

**Status date:** 26 August 2026  
**Target board:** Raspberry Pi 3 Model B / Model B+  
**Target architecture:** AArch64 (`arm64`)  
**Current result:** TF-A, OP-TEE OS, U-Boot, Linux `Image`, and the Raspberry Pi 3 DTBs exist. Step 3 is complete. Boot-files packaging and real-board OP-TEE boot validation have not yet been performed.

---

## 1. Project objective

The project is preparing a Raspberry Pi 3 platform for experiments involving ARM TrustZone, OP-TEE, and later secure CAN-packet validation.

TrustZone divides the processor's execution environment conceptually into two worlds:

```text
                         ARM TrustZone
                              |
              +---------------+---------------+
              |                               |
         Secure world                    Normal world
              |                               |
          OP-TEE OS                      U-Boot/Linux
          Trusted Apps                   Client Apps
              |                               |
              +---------- SMC/TEE ------------+
```

- **Secure world:** runs OP-TEE OS and, later, Trusted Applications (TAs).
- **Normal world:** runs U-Boot, Linux, ordinary applications, and OP-TEE client applications.
- **Secure Monitor Calls (SMCs):** controlled transitions between the two worlds.
- **Linux TEE subsystem:** provides the normal-world kernel interface to OP-TEE.

The intended boot/software chain is:

```text
Raspberry Pi Boot ROM and VideoCore firmware
                    |
                    v
       armstub8.bin: TF-A BL1 + padded FIP
                    |
                    +--> TF-A runtime / secure monitor
                    +--> BL32: OP-TEE OS (secure world)
                    +--> BL33: U-Boot (normal world)
                                      |
                                      v
                         Linux Image + selected DTB
                                      |
                                      v
                              Linux user space
```

This is more precise than writing only `TF-A -> U-Boot -> Linux`, because the TF-A FIP is built with both OP-TEE and U-Boot payloads.

---

## 2. Important security limitation of this platform

The local TF-A Raspberry Pi 3 documentation explicitly states that its RPi3 port is a **prototype and is not a secure production platform**.

The reason is that the emulated secure and non-secure memory regions are all in normal DRAM. The Raspberry Pi 3 VideoCore/memory architecture does not provide the same strong physical memory isolation expected from a production TrustZone platform.

Therefore, this project can demonstrate:

- the TrustZone execution model;
- TF-A secure-monitor transitions;
- OP-TEE boot and communication;
- Trusted Application design;
- Linux/OP-TEE integration;
- a research prototype for secure CAN validation.

It must not claim that Raspberry Pi 3 provides production-grade physical secure-memory protection.

---

## 3. Actual project locations

### Raspberry Pi

The authoritative multi-repository OP-TEE checkout is:

```text
/home/aslam/Minor/optee_project
```

Important subdirectories:

```text
optee_project/
|-- build/                 OP-TEE manifest build logic and rpi3.mk
|-- buildroot/             Minimal Linux root filesystem builder
|-- firmware/              Raspberry Pi stock firmware files
|-- linux/                 Raspberry Pi Linux kernel source
|-- optee_client/          Normal-world OP-TEE client library/daemon
|-- optee_examples/        Example client applications and TAs
|-- optee_os/              Secure-world OP-TEE OS
|-- optee_test/            OP-TEE test suite
|-- toolchains/            Downloaded cross-toolchains
|-- trusted-firmware-a/    TF-A secure monitor and early firmware
`-- u-boot/                Normal-world bootloader
```

### Laptop

The laptop working directory is:

```text
/home/aslam/Minor_Project
```

The Pi Linux tree was copied to:

```text
/home/aslam/Minor_Project/optee_project/linux
```

The clean out-of-tree Linux build directory is:

```text
/home/aslam/Minor_Project/build/linux-rpi3
```

The collected Step 3 deliverables are:

```text
/home/aslam/Minor_Project/Step3_Output/
|-- Image
|-- linux.config
`-- dtbs/
    |-- bcm2710-rpi-3-b.dtb
    `-- bcm2710-rpi-3-b-plus.dtb
```

---

## 4. Source revisions found on the Raspberry Pi

The inspected repository revisions are:

```text
trusted-firmware-a  a1f02f4f3  Merge "docs(changelog): generate changelog" into integration
optee_os            d5001fe0a  plat-qcom: hoya: lemans: enable PAS firmware authentication
u-boot              d80bb749   Prepare v2021.10
linux               8bfce8398  rpi3: Enable PSCI support
build               f441904    build: share common helpers between QEMU expect scripts
```

Recording revisions is important because build behavior depends on the exact source versions, not only component names.

---

# STEP 1: OP-TEE OS and Trusted Firmware-A

## 5. What OP-TEE OS does

OP-TEE OS is the secure-world operating system. In TF-A terminology it is supplied as **BL32**, the secure payload.

The Raspberry Pi contains these verified OP-TEE outputs:

```text
~/Minor/optee_project/optee_os/out/arm/core/tee.bin
~/Minor/optee_project/optee_os/out/arm/core/tee.elf
~/Minor/optee_project/optee_os/out/arm/core/tee-header_v2.bin
~/Minor/optee_project/optee_os/out/arm/core/tee-pager_v2.bin
~/Minor/optee_project/optee_os/out/arm/core/tee-pageable_v2.bin
```

Observed sizes:

```text
tee.bin               532,228 bytes
tee.elf             4,754,568 bytes
tee-header_v2.bin          28 bytes
tee-pager_v2.bin      532,200 bytes
tee-pageable_v2.bin         0 bytes
```

The zero-byte pageable file is not automatically an error for this particular output arrangement: the pager binary contains the generated payload and the header describes the split images supplied to TF-A.

### Important distinction

The presence of these files proves that **OP-TEE OS compiled**. It does not prove that OP-TEE has booted successfully on the physical Raspberry Pi.

Real boot proof must later include serial-console or kernel evidence such as OP-TEE initialization messages and working client/TA communication.

## 6. What Trusted Firmware-A does

TF-A provides the early AArch64 firmware stages and secure monitor/runtime services. For this build:

- platform is `rpi3`;
- secure payload dispatcher is `opteed`;
- OP-TEE is BL32;
- U-Boot is BL33;
- debug logging is enabled;
- Cortex-A53 is the CPU target.

Relevant `build/rpi3.mk` settings are conceptually:

```make
PLAT=rpi3
SPD=opteed
NEED_BL32=yes
BL32=tee-header_v2.bin
BL32_EXTRA1=tee-pager_v2.bin
BL32_EXTRA2=tee-pageable_v2.bin
BL33=u-boot.bin
DEBUG=1
RPI3_PRELOADED_DTB_BASE=0x00010000
```

TF-A's `tf-a` target depends on `optee-os` and `u-boot`. This ordering ensures that BL32 and BL33 exist before TF-A packages the FIP.

## 7. TF-A outputs

Verified on the Raspberry Pi:

```text
~/Minor/optee_project/trusted-firmware-a/build/rpi3/debug/armstub8.bin
~/Minor/optee_project/trusted-firmware-a/build/rpi3/debug/fip.bin
```

Exact observed sizes:

```text
armstub8.bin    1,278,984 bytes (about 1.3 MB)
fip.bin         1,147,912 bytes (about 1.1 MB)
```

SHA-256 values:

```text
armstub8.bin  90a3429ba9af1be704aab2c34a9708f0c12e8a8293bf00b9ba4a23aeebc15808
fip.bin       a5f581b16f54a619a0801dd1443a58a0061b6fe203c32ed88b999b232689e6b5
```

## 8. Exact relationship between `armstub8.bin` and `fip.bin`

This is an important correction to the earlier summary.

For the TF-A Raspberry Pi 3 port, `armstub8.bin` is not merely an unrelated boot stub. The TF-A platform rule does this:

1. copy `bl1.bin` to `bl1_pad.bin`;
2. pad BL1 to 131,072 bytes (`0x20000`);
3. concatenate `fip.bin` after padded BL1;
4. write the result as `armstub8.bin`.

Conceptually:

```text
armstub8.bin = padded BL1 + fip.bin
```

The TF-A documentation confirms that `armstub8.bin` contains BL1 and the FIP. Consequently, `build/rpi3.mk` copies `armstub8.bin` to the boot partition but does **not** copy `fip.bin` as a separate boot-partition file.

The FIP contains the later firmware images, including the configured BL32 OP-TEE payload and BL33 U-Boot payload.

## 9. Why Raspberry Pi firmware uses `armstub8.bin`

The VideoCore is the Raspberry Pi's primary boot processor. For an AArch64 boot:

- with `kernel8.img`, it starts the ARM cores in AArch64;
- if `armstub8.bin` is present, it loads that file at address `0x0` and enters it at EL3;
- this allows TF-A to control the secure monitor and later world transitions.

The local TF-A documentation gives this relevant memory layout:

```text
0x00000000  BL1 / armstub start
0x00020000  FIP
0x01000000  DTB
0x02000000  Linux kernel image
0x10000000  TF-A secure SRAM area (BL2/BL31)
0x10100000  secure DRAM area (BL32 / OP-TEE)
0x11000000  non-secure DRAM area (BL33 / U-Boot)
```

## 10. Step 1 result

```text
OP-TEE secure-world binaries generated    PASS
TF-A armstub8.bin generated               PASS
TF-A fip.bin generated                    PASS
OP-TEE included as BL32 configuration     PASS
U-Boot included as BL33 configuration     PASS
Physical-board secure boot proven         NOT YET
```

---

# STEP 2: U-Boot

## 11. What U-Boot does

U-Boot is the normal-world bootloader used as TF-A's BL33 payload. After TF-A initializes the secure monitor and enters the normal world, U-Boot prepares Linux boot arguments and starts Linux.

The U-Boot configuration is produced from:

```text
u-boot/configs/rpi_3_defconfig
        +
build/kconfigs/u-boot_rpi3.conf
        |
        v
u-boot/.config
```

The build uses:

```text
ARCH=arm64
CROSS_COMPILE=<AArch64 toolchain prefix>
```

## 12. U-Boot output

Verified on the Raspberry Pi:

```text
~/Minor/optee_project/u-boot/u-boot.bin
```

Observed size:

```text
540,496 bytes (reported by ls as approximately 528 KB)
```

SHA-256:

```text
95345a66cfb9fc3757664cc99e19e0e8ff88a149f2fbcb72aa98fb90419bebba
```

`file` may misidentify raw firmware binaries because they have no standard executable container header. Successful U-Boot build output, size, Makefile integration, and later boot behavior are better evidence than the generic `file` heuristic for `u-boot.bin`.

## 13. U-Boot environment and Linux loading plan

The source environment is:

```text
~/Minor/optee_project/build/rpi3/firmware/uboot.env.txt
```

Its important logic is:

```text
kernel_addr_r = 0x02000000
fdt_addr_r    = 0x01000000
```

It selects the DTB based on the board name:

```text
3 Model B   -> bcm2710-rpi-3-b.dtb
3 Model B+  -> bcm2710-rpi-3-b-plus.dtb
```

It loads:

```text
kernel8.img from FAT partition mmc 0:1
the selected DTB from FAT partition mmc 0:1
```

Then it executes:

```text
booti ${kernel_addr_r} - ${fdt_addr_r}
```

The root filesystem is expected on:

```text
/dev/mmcblk0p2
```

## 14. Step 2 result

```text
U-Boot configuration generated      PASS
u-boot.bin generated                PASS
U-Boot configured as TF-A BL33      PASS
Generated binary uboot.env          NOT YET
U-Boot physical-board execution     NOT YET PROVEN
```

---

# STEP 3: Linux kernel and Raspberry Pi 3 DTBs

## 15. Step 3 objective

The Step 3 target was strictly:

1. obtain a valid Raspberry Pi 3 ARM64 Linux configuration;
2. enable Linux TEE and OP-TEE support;
3. build the uncompressed ARM64 kernel `Image`;
4. build the Raspberry Pi 3 Model B and Model B+ DTBs;
5. place the verified outputs back in the Pi's Linux tree.

Step 3 did not include SD-image packaging, physical booting, Trusted Application development, or CAN validation.

## 16. Initial Linux-tree problem

The Linux source location on the Pi is:

```text
~/Minor/optee_project/linux
```

Initial `make linux` attempts failed during configuration because directories and files tracked in Git were absent from the checked-out working tree.

The first visible failure was:

```text
scripts/kconfig/merge_config.sh: No such file or directory
```

Evidence showed a mismatch between Git's commit tree and the working directory. For example:

```bash
git ls-tree HEAD scripts
```

showed that `scripts` existed in the commit, while filesystem checks could not find it.

The missing content was restored from the current commit, including commands such as:

```bash
git restore --source=HEAD -- scripts/
git restore --source=HEAD -- usr/
git restore --source=HEAD -- net/
```

Other configuration failures exposed missing content under areas such as:

```text
init/
kernel/
virt/
```

The key diagnosis was not “Kconfig is broken.” It was:

```text
Git commit contains the source
            +
working directory is incomplete
            =
Kconfig cannot open referenced files
```

After the tracked source content was restored, the previous Kconfig errors were no longer the active problem.

## 17. How the Linux configuration is generated

The Raspberry Pi build file defines:

```text
LINUX_DEFCONFIG_COMMON_ARCH = arm64
```

and merges:

```text
linux/arch/arm64/configs/bcmrpi3_defconfig
                        +
build/kconfigs/rpi3.conf
                        |
                        v
                  linux/.config
```

The configuration fragment contains:

```text
CONFIG_TEE=y
CONFIG_OPTEE=y
```

The merge is performed by:

```text
scripts/kconfig/merge_config.sh
```

The generated Pi configuration exists at:

```text
~/Minor/optee_project/linux/.config
```

Observed size:

```text
215,894 bytes (about 211 KB)
```

## 18. Verified kernel configuration

The important verified settings are:

```text
CONFIG_LOCALVERSION="-v8"
CONFIG_ARM64=y
CONFIG_ARCH_BCM=y
CONFIG_ARCH_BCM2835=y
CONFIG_RASPBERRYPI_FIRMWARE=y
CONFIG_TEE=y
CONFIG_OPTEE=y
# CONFIG_OPTEE_INSECURE_LOAD_IMAGE is not set
```

Meaning:

- `CONFIG_ARM64=y`: the kernel targets 64-bit Arm.
- `CONFIG_ARCH_BCM=y`: Broadcom SoC support is enabled.
- `CONFIG_ARCH_BCM2835=y`: the Broadcom family used by Raspberry Pi support is enabled.
- `CONFIG_RASPBERRYPI_FIRMWARE=y`: Linux can communicate with Raspberry Pi firmware interfaces.
- `CONFIG_TEE=y`: the generic Linux Trusted Execution Environment framework is built into the kernel.
- `CONFIG_OPTEE=y`: the OP-TEE kernel driver is built into the kernel.
- insecure OP-TEE image loading is disabled.

These settings prove Linux-side OP-TEE support. They do not prove a responding secure-world OP-TEE instance.

## 19. Why the native Pi build was stopped

The Pi has approximately:

```text
RAM:   905 MiB
Swap:  about 904 MiB
CPU:   four Cortex-A53 cores
```

The build was observed running real compiler work:

```bash
ps -eo pid,etime,%cpu,%mem,cmd | grep '[c]c1'
```

High `cc1` CPU usage and output such as `CC mm/...` proved that compilation was progressing rather than frozen.

However, a full kernel build on a 1 GB Raspberry Pi is unnecessarily slow and creates memory pressure. It was safe to stop `make` with `Ctrl+C`; this does not damage the source tree.

## 20. Important correction: where the final kernel was built

The final successful kernel was **not completed natively on the Raspberry Pi**.

The actual procedure was:

1. inspect and preserve the correct Pi `.config`;
2. confirm no final Pi `Image` or requested DTBs existed;
3. copy the Pi Linux source tree to the laptop using SSH/rsync;
4. confirm the laptop had an AArch64 cross-compiler;
5. preserve the `.config` byte-for-byte;
6. remove stale Pi-native generated host tools from only the laptop copy;
7. create an isolated out-of-tree build directory;
8. run `olddefconfig` for ARM64;
9. cross-compile `Image` and `dtbs` on the laptop;
10. verify the outputs;
11. copy the verified outputs back into the Pi's expected Linux paths;
12. compare SHA-256 hashes between laptop and Pi.

The Pi's original source and configuration were not rebuilt or cleaned during this laptop build.

## 21. Laptop cross-compiler

The laptop compiler used for the final build was:

```text
/usr/bin/aarch64-linux-gnu-gcc
aarch64-linux-gnu-gcc 13.3.0
```

The effective build variables were:

```text
ARCH=arm64
CROSS_COMPILE=aarch64-linux-gnu-
```

The project also has an AArch64 toolchain on the Pi under:

```text
~/Minor/optee_project/toolchains/aarch64/bin/
```

but the final laptop build used the laptop's installed `/usr/bin` cross-compiler.

## 22. Clean out-of-tree build method

The Pi source included partial generated objects and host utilities. Host build tools created on ARM cannot be executed on the x86-64 laptop.

To prevent architecture mixing, the procedure used:

```text
Source:
/home/aslam/Minor_Project/optee_project/linux

Output:
/home/aslam/Minor_Project/build/linux-rpi3
```

The `.config` was copied into the output directory. Stale generated content was cleaned only from the laptop's copied source, and configuration regeneration succeeded with:

```bash
make -C /home/aslam/Minor_Project/optee_project/linux \
  O=/home/aslam/Minor_Project/build/linux-rpi3 \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  olddefconfig
```

The kernel and DTBs were built with four parallel jobs:

```bash
make -C /home/aslam/Minor_Project/optee_project/linux \
  O=/home/aslam/Minor_Project/build/linux-rpi3 \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  -j4 Image dtbs
```

Four jobs balanced build speed with the laptop's available memory.

The final build ended with:

```text
LD      vmlinux
NM      System.map
SORTTAB vmlinux
OBJCOPY arch/arm64/boot/Image
```

The resulting kernel release is:

```text
6.7.0-rc6-v8
```

## 23. Final Linux artifacts

### Kernel Image

```text
File: arch/arm64/boot/Image
Size: 22,993,408 bytes (about 22 MB)
Type: Linux kernel ARM64 boot executable Image, little-endian, 4K pages
SHA-256: 0ae1b1d04cbf0e411856f76ab004c78833a8d115b867179a68dd439a710c9f73
```

### Raspberry Pi 3 Model B DTB

```text
File: arch/arm64/boot/dts/broadcom/bcm2710-rpi-3-b.dtb
Size: 33,616 bytes (about 33 KB)
Model: Raspberry Pi 3 Model B
SHA-256: ce000aea52296130db2c6379d1af56ac65420b1a3fdc9346407cf6a3ea81fab3
```

### Raspberry Pi 3 Model B+ DTB

```text
File: arch/arm64/boot/dts/broadcom/bcm2710-rpi-3-b-plus.dtb
Size: 34,235 bytes (about 34 KB)
Model: Raspberry Pi 3 Model B+
SHA-256: 4821bfa78639481065d01ea810c83895ec7dfab1a8c9828f87761387a38f08ae
```

## 24. Copying the final outputs back to the Pi

After laptop verification, the three files were copied over SSH to:

```text
~/Minor/optee_project/linux/arch/arm64/boot/Image
~/Minor/optee_project/linux/arch/arm64/boot/dts/broadcom/bcm2710-rpi-3-b.dtb
~/Minor/optee_project/linux/arch/arm64/boot/dts/broadcom/bcm2710-rpi-3-b-plus.dtb
```

The laptop and Pi SHA-256 values matched exactly. This proves the transfer did not alter the binaries.

## 25. What a DTB does

The kernel `Image` contains generic compiled kernel code. It cannot discover every board-specific connection by probing.

A Device Tree Blob describes hardware such as:

- CPU topology;
- memory layout;
- interrupt controllers;
- UARTs;
- GPIO controllers;
- clocks;
- USB and Ethernet-related devices;
- device addresses and compatibility strings.

U-Boot selects the correct Pi 3 Model B or Model B+ DTB and passes its memory address to Linux during `booti`.

## 26. Step 3 verification result

```text
Linux source available                         PASS
Missing tracked source recovered              PASS
merge_config.sh available                     PASS
bcmrpi3_defconfig merged                      PASS
rpi3.conf merged                              PASS
ARM64/Broadcom configuration                  PASS
CONFIG_TEE=y                                  PASS
CONFIG_OPTEE=y                                PASS
Laptop AArch64 cross-build                    PASS
Linux Image                                   PASS
Raspberry Pi 3 Model B DTB                    PASS
Raspberry Pi 3 Model B+ DTB                   PASS
Artifacts copied back to Pi                   PASS
Laptop/Pi checksums identical                 PASS
```

Therefore:

```text
STEP 3 = COMPLETE
```

---

## 27. Complete artifact inventory after Step 3

```text
OP-TEE OS
|-- tee.bin                         built
|-- tee.elf                         built
|-- tee-header_v2.bin               built
|-- tee-pager_v2.bin                built
`-- tee-pageable_v2.bin             built (zero-byte split component)

TF-A
|-- armstub8.bin                    built
`-- fip.bin                         built and included within armstub8 layout

U-Boot
`-- u-boot.bin                      built and packaged as TF-A BL33

Linux
|-- .config                         generated
|-- arch/arm64/boot/Image           built
|-- bcm2710-rpi-3-b.dtb             built
`-- bcm2710-rpi-3-b-plus.dtb        built
```

---

## 28. What has not been completed or proven

The following are deliberately not marked complete:

```text
Linux modules installed into module_output       NOT DONE
Buildroot root filesystem finalized              NOT VERIFIED/DONE
Generated binary uboot.env                       NOT DONE
out/boot populated                               NOT DONE
out/rpi3-sdcard.img generated                    NOT DONE
Image written to a target OP-TEE SD card         NOT DONE
TF-A serial output observed on target boot       NOT DONE
OP-TEE secure-world boot observed                NOT DONE
Linux OP-TEE driver bound to secure world        NOT DONE
xtest/client-to-TA communication demonstrated    NOT DONE
CAN secure validation application                NOT STARTED IN THESE STEPS
```

At audit time, this file did not exist:

```text
~/Minor/optee_project/out/rpi3-sdcard.img
```

This is why the next stage is packaging, not another Linux compilation.

---

# STEP 4: next stage, not yet executed

## 29. What `update_bootfs` will assemble

According to `build/rpi3.mk`, the boot-files target will place these into `out/boot`:

```text
bcm2710-rpi-3-b.dtb
bcm2710-rpi-3-b-plus.dtb
config.txt
kernel8.img             <- copied from Linux arch/arm64/boot/Image
armstub8.bin            <- TF-A BL1 + padded FIP
uboot.env               <- generated binary U-Boot environment
bootcode.bin
COPYING.linux
fixup_cd.dat
fixup.dat
fixup_db.dat
fixup_x.dat
LICENCE.broadcom
start_cd.elf
start_db.elf
start.elf
start_x.elf
```

The `config.txt` source contains:

```text
enable_uart=1
kernel_address=0x02000000
device_tree_address=0x01000000
```

The `all` target is designed to run:

```text
tf-a
buildroot
optee-os
u-boot
linux
update_bootfs
update_rootfs
sdcard-image
```

However, the next command should be chosen carefully because a normal `linux` dependency may attempt additional kernel/module work. Step 4 should first inspect timestamps/dependencies and select the packaging target deliberately rather than blindly rebuilding everything.

No SD-card write command should be run until the exact removable target device is independently verified.

---

## 30. How to explain the complete work orally

### Short explanation

> We prepared the Raspberry Pi 3 OP-TEE boot components through the Linux stage. OP-TEE OS was built as TF-A's BL32 secure payload, U-Boot was built as BL33, and TF-A packaged these into a FIP that is concatenated with BL1 to form `armstub8.bin`. We recovered an incomplete Linux working tree, generated the ARM64 Raspberry Pi configuration by merging `bcmrpi3_defconfig` with the OP-TEE fragment, and verified `CONFIG_TEE=y` and `CONFIG_OPTEE=y`. Because the Pi has only about 1 GB RAM, we moved the final Linux compilation to the laptop, cross-built kernel 6.7.0-rc6-v8 and both Raspberry Pi 3 DTBs, verified them, and copied them back to the Pi with matching checksums. Packaging and real OP-TEE boot validation are the next steps.

### One-line meeting statement

> By Step 3, we built TF-A with OP-TEE as BL32 and U-Boot as BL33, cross-compiled an OP-TEE-enabled ARM64 Linux kernel and Raspberry Pi 3 DTBs, and verified identical artifacts on the laptop and Raspberry Pi; SD-image packaging and runtime secure-world validation remain.

---

## 31. Questions an examiner may ask

### Why is `CONFIG_OPTEE=y` not proof that OP-TEE runs?

It only compiles the normal-world Linux driver. A secure-world OP-TEE instance must boot and answer SMC requests before communication works.

### Why use TF-A?

TF-A provides early firmware stages, EL3 secure-monitor services, PSCI support, and dispatching between normal and secure worlds through `opteed`.

### What are BL32 and BL33?

- BL32 is the secure payload: OP-TEE OS.
- BL33 is the non-secure payload: U-Boot.

### Why is `fip.bin` not copied separately to the SD boot partition?

The RPi3 TF-A rule pads BL1 and concatenates `fip.bin` into `armstub8.bin`. The boot partition therefore needs the resulting `armstub8.bin`.

### Why is the Linux file renamed to `kernel8.img`?

The Raspberry Pi firmware recognizes `kernel8.img` as the AArch64 payload filename. The packaging rule copies Linux `Image` under that name.

### Why are two DTBs built?

The project supports Raspberry Pi 3 Model B and Model B+. U-Boot examines `board_name` and selects the corresponding DTB.

### Why cross-compile on the laptop?

The target is ARM64, but the laptop has much more RAM and faster CPU resources. An AArch64 cross-compiler produces target ARM64 binaries while running on the x86-64 laptop.

### Why use an out-of-tree build directory?

It separates generated files from source, avoids executing ARM-built host utilities on x86-64, preserves configuration cleanly, and makes outputs easier to inspect.

### What is the next technical proof?

Generate the boot/root filesystem image, boot it with serial logging, confirm TF-A and OP-TEE messages, confirm the Linux OP-TEE driver, and run a client/TA test.

---

## 32. Final status table

| Item | Status |
|---|---|
| OP-TEE OS binaries | Complete |
| TF-A `armstub8.bin` | Complete |
| TF-A `fip.bin` | Complete |
| U-Boot `u-boot.bin` | Complete |
| Linux `.config` | Complete |
| ARM64/Broadcom support | Enabled |
| Linux generic TEE framework | Built in |
| Linux OP-TEE driver | Built in |
| Linux ARM64 `Image` | Complete |
| Pi 3 Model B DTB | Complete |
| Pi 3 Model B+ DTB | Complete |
| Artifacts copied to Pi | Complete |
| Boot filesystem assembly | Not done |
| Root filesystem/image assembly | Not done |
| Physical OP-TEE boot | Not proven |
| Client/TA communication | Not proven |
| Secure CAN validation | Not started in Steps 1-3 |

**Conclusion:** Step 3 is complete. Do not rebuild Linux merely to re-verify it. Proceed next with controlled packaging and boot validation.
