# Step 10 Completion — Project-Specific Trusted Application

**Verified:** 7 September 2026

Step 10 is complete on the Raspberry Pi 3 using the 8 GB test card.

## Components

The project now owns both sides of the OP-TEE interface:

```text
secure_can_demo/host/main.c
        | TEEC_OpenSession / TEEC_InvokeCommand
        v
secure_can_demo/ta/secure_can_ta.c
```

The unique TA UUID is:

```text
abecf637-ca20-467e-8e6c-0b8779ba6b2f
```

Both the host and TA ELF payload are AArch64. The TA was signed by the OP-TEE
TA development kit.

## Physical runtime result

The client and signed TA were installed on the physical Raspberry Pi and the
client returned zero. The saved `Step10-runtime.log` contains:

```text
Normal World challenge: 0x12345678
Secure World response: 0xb791f3dd
Secure World marker:   0x5343414e (SCAN)
PASS: project-specific Normal World to Secure World round trip
```

This proves that the project-specific client opened a session, invoked the TA,
transferred a value into Secure World, and validated values returned from
Secure World.

## Persistent image

The tested client and TA were added to the persistent Buildroot overlay and a
new SD-card image was generated. The files extracted from the root filesystem
image matched the tested build artifacts byte-for-byte.

```text
Image SHA-256:
5f479a006b8f48ea422420dd7bb6a0c102541f80742fd3c06ede9679da3d5f7a

Host SHA-256:
4de3897e53b1515822881f140a922637a0e33ab99b0fb5045d0b10bc748ab548

TA SHA-256:
b30301bcc5b04f963c44d9c82c606d4e478b93c284d51a37e4e1f6a108a3fe7e
```

Step 10 is the completed gate before CAN hardware selection and secure CAN
protocol development in Step 11.
