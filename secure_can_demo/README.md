# Secure CAN OP-TEE Demonstration

This Step 10 application proves that a project-owned Normal World client can
open a session with a project-owned Trusted Application, send a challenge
across the TrustZone boundary, and validate the Secure World response.

## Interface

- TA UUID: `abecf637-ca20-467e-8e6c-0b8779ba6b2f`
- Command: `TA_SECURE_CAN_CMD_PROCESS_CHALLENGE`
- Challenge: `0x12345678`
- Secure transformation: challenge XOR `0xa5a5a5a5`
- Expected response: `0xb791f3dd`
- Secure marker: `0x5343414e` (`SCAN`)

The transformation is only a deterministic communication test. It is not the
future CAN authentication algorithm.

## Build

Run from `/home/aslam/Minor_Project`:

```sh
make -C secure_can_demo -j4 \
  HOST_CROSS_COMPILE=/home/aslam/Minor_Project/optee_full/out-br/host/bin/aarch64-linux-gnu- \
  TA_CROSS_COMPILE=/home/aslam/Minor_Project/optee_full/toolchains/aarch64/bin/aarch64-linux-gnu- \
  TEEC_EXPORT=/home/aslam/Minor_Project/optee_full/out-br/host/aarch64-buildroot-linux-gnu/sysroot/usr \
  TA_DEV_KIT_DIR=/home/aslam/Minor_Project/optee_full/optee_os/out/arm/export-ta_arm64
```

## Run on Raspberry Pi

The final generated image installs both files automatically. Run:

```sh
secure_can_demo
```

Success ends with:

```text
PASS: project-specific Normal World to Secure World round trip
```
