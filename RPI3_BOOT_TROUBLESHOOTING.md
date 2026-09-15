# Raspberry Pi 3 OP-TEE Boot Troubleshooting

Use this guide when the custom OP-TEE SD card does not boot, the display
disappears, or the LEDs behave unexpectedly.

This project image supports only **Raspberry Pi 3 Model B and Model B+**. It is
not a Raspberry Pi 4, Pi 5, Pi Zero, or Pi 3 A+ image.

## Important safety rule

Never guess the SD-card device name. Writing an image to the wrong device will
destroy its existing data. In this system, `/dev/nvme0n1` is the laptop system
disk and must never be used as the `dd` output.

## Quick diagnosis

| Symptom | Meaning or likely cause | Next action |
| --- | --- | --- |
| Red `PWR` LED is off | No power | Check the supply, cable, and connector |
| Red `PWR` LED blinks | Low or unstable voltage | Use a reliable 5.1 V, 2.5 A or stronger supply and disconnect peripherals |
| Red LED is solid; green `ACT` never blinks | SD card is not being read | Reseat the card, verify the flash, or try another card |
| Green LED blinks initially, then stops | Firmware read the card; stopping can be normal or the boot chain may have stalled | Observe HDMI and capture UART output if Linux does not start |
| HDMI appears and then disappears | Boot reached firmware or a later stage, then changed video mode, stalled, or crashed | Verify the image, then capture the UART boot log |
| Another SD card boots normally | Pi and power are probably usable | Concentrate on the custom image, flash, or SD card |
| Ethernet LEDs remain off | Boot may not have reached Linux/network initialization | Fix the earlier boot failure first |

The secure-timer handler runs much later than the Raspberry Pi firmware. If
there is no initial green activity at all, the timer code is not the cause.

## 1. Check the hardware first

1. Turn the Raspberry Pi off before inserting or removing the microSD card.
2. Confirm the board says `Raspberry Pi 3 Model B` or `Model B+`.
3. Reseat the microSD card fully.
4. Initially disconnect USB and CAN hardware. Leave only the SD card, HDMI,
   and, if required, Ethernet connected.
5. Use a stable 5.1 V, 2.5 A or stronger supply and a short, good-quality
   micro-USB cable.
6. The red `PWR` LED should remain solid during boot.

If a known-good Raspberry Pi OS card boots using the same Pi, supply, cable,
and display, continue with the custom-card checks below.

## 2. Identify the SD card safely

Insert the card into the laptop and run:

```sh
findmnt -rn -o SOURCE,TARGET /
lsblk -b -o NAME,PATH,SIZE,MODEL,SERIAL,TRAN,RM,RO,TYPE,FSTYPE,LABEL,MOUNTPOINTS
```

Confirm all of the following before continuing:

- the candidate is a removable disk (`RM=1`), normally connected by USB;
- its capacity matches the SD card;
- it is not the device that contains `/`; and
- you will target the whole disk, such as `/dev/sda`, not `/dev/sda1`.

During the September 2026 test, the verified card was `/dev/sda`, while the
laptop root filesystem was `/dev/nvme0n1p6`. Device names can change after
unplugging or rebooting, so recheck every time.

## 3. Unmount and flash

The current image is:

```text
/home/aslam/Minor_Project/optee_full/out/rpi3-sdcard.img
```

Its expected SHA-256 for commit `9006265` is:

```text
fbb604c632624512ae04f62f8f9ebc8dfbba940c6e991c21e5357410827a58d1
```

Verify the image file first:

```sh
sha256sum /home/aslam/Minor_Project/optee_full/out/rpi3-sdcard.img
```

After replacing `/dev/sdX` below with the newly verified removable whole-disk
device, unmount its partitions and flash it:

```sh
udisksctl unmount -b /dev/sdX1
udisksctl unmount -b /dev/sdX2

sudo dd if=/home/aslam/Minor_Project/optee_full/out/rpi3-sdcard.img \
  of=/dev/sdX bs=4M conv=fsync status=progress
sync
```

Do not remove the card until `dd` and `sync` have both finished.

## 4. Verify the written card byte for byte

Verification is required even when `dd` reports success. Replace `/dev/sdX`
with the same verified whole-disk device:

```sh
sudo cmp --bytes="$(stat -c %s /home/aslam/Minor_Project/optee_full/out/rpi3-sdcard.img)" \
  /home/aslam/Minor_Project/optee_full/out/rpi3-sdcard.img /dev/sdX \
  && echo "VERIFICATION PASS"
```

Expected result:

```text
VERIFICATION PASS
```

If `cmp` prints a difference or an input/output error:

1. flash the image again;
2. repeat the comparison;
3. clean the card contacts and try again; and
4. replace the SD card or reader if the failure repeats.

After a successful comparison, safely power off the reader:

```sh
udisksctl power-off -b /dev/sdX
```

## 5. Boot with minimum hardware

1. Ensure the Pi is powered off.
2. Insert the verified card.
3. Connect HDMI and optionally Ethernet.
4. Apply power and watch the red and green LEDs.

A green LED does not need to blink continuously; it indicates SD-card
activity. The important failure signs are no initial green activity, a
blinking red LED, or a boot that never reaches Linux.

## 6. If the verified image still stops

Capture the early serial log. Use only a **3.3 V USB-to-TTL adapter**, never an
RS-232-level adapter and never connect the adapter's power pin.

| Adapter | Raspberry Pi 3 header |
| --- | --- |
| GND | GND, for example physical pin 6 |
| Adapter RX | Pi GPIO14/TX, physical pin 8 |
| Adapter TX | Pi GPIO15/RX, physical pin 10 |

Open the adapter at `115200 8N1`, then power on the Pi and save the complete
output. The last visible stage identifies whether the stop occurs in Raspberry
Pi firmware, TF-A, OP-TEE, U-Boot, the Linux kernel, or the root filesystem.

## Recovery checklist

- [ ] Correct board: Raspberry Pi 3 Model B/B+
- [ ] Red power LED remains solid
- [ ] Known-good OS card boots on the same hardware
- [ ] SD target identified as removable and is not the root disk
- [ ] Correct image SHA-256
- [ ] `dd` and `sync` completed
- [ ] Byte-for-byte comparison passed
- [ ] Card safely ejected and firmly reseated
- [ ] Minimum peripherals used for the first boot
- [ ] UART log captured if the verified image still stops

