# candump_aarch64 (static, for the receiver)

`candump` from linux-can/can-utils v2023.03 (tarball sha256 `2790dfb57fe3ec22b6fd512838c6480c39f7c9ae193e59f1ae01221216505a7e`), built on the laptop for the receiver (aarch64, static, no libraries needed):

    aarch64-linux-gnu-gcc -static -O2 -DSO_RXQ_OVFL=40 -DPF_CAN=29 -DAF_CAN=PF_CAN -I. -o candump_aarch64 candump.c lib.c

sha256 of the binary: `dba13eebc6cd0c95ad3ecc7d47e7ad23a37a8964d3047996ced3409efd08f591`. It is the binary used for the captures in `baseline/real_bus_step3b_*` and `real_bus_step4_*`
(option `-td -x` shows the time delta and the BRS/ESI flags). The receiver image also has a system `candump` in `/usr/bin`; its version was not checked, so the watch
instructions use this static copy, copied into the receiver's RAM disk (`/tmp`, gone after a reboot).
