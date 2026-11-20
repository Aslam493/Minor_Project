# Permanent CAN-FD on the receiver (applied 2026-11-21, user approved)

BusyBox `ip` on the receiver accepts the word `dbitrate` but never sends it to the kernel, which then refuses `fd on` (EOPNOTSUPP). So the boot script
`S12can` calls `canfdcfg <iface> bus` (500000 nominal, 2000000 data, FD on, no loopback, up) when `/usr/sbin/canfdcfg` exists, and falls back to the
old classic path if it does not.

Applied on the running receiver (root filesystem of the 8 GB card): `/usr/sbin/canfdcfg` (this folder's `canfdcfg_aarch64`, sha256 178546af03b25d18...ada4f6),
`/etc/init.d/S12can` (this folder's `S12can`; diff against `S12can.orig-classic`). Backup of the old script on the Pi: `/root/S12can.orig-classic`
(NOT in /etc/init.d: every S* file there is executed at boot). Verified after a reboot of both Pis: baseline/fd_permanent_after_reboot_20261121.txt.

NOT done (your decision): the Buildroot overlay `optee_full/build/br-ext/board/rpi3/overlay/etc/init.d/S12can` and the binary are not yet part of the image build.
A new image composed from the Sep-20 base would carry the OLD S12can (classic 500000), like the DTB lesson (compose_image.sh): add `S12can` and
`usr/sbin/canfdcfg` to the overlay (or to compose_image.sh) before the next flash.
Rollback on the Pi: `cp /root/S12can.orig-classic /etc/init.d/S12can` (or delete /usr/sbin/canfdcfg; S12can then falls back to classic).
Sender: `/etc/systemd/system/can0-up.service` (FD 500000/2000000); old unit kept as `can0-up.service.bak-classic`.
