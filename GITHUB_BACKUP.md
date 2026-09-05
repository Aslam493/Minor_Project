# GitHub Backup Scope

This repository preserves the Minor Project through completed Step 7.

Included:

- roadmap, progress notes, reports, presentations, and generation scripts;
- Step 3 through Step 7 verification records;
- build logs and SHA-256 checksum manifests;
- the Step 7 image-generation log and SD-card write record;
- curated OP-TEE, TF-A, U-Boot, Linux, and DTB verification artifacts;
- the complete assembled `optee_full/out/boot` directory;
- `optee_full/out-br/images/rootfs.cpio.gz` for root filesystem recovery; and
- exact upstream component Git revisions in `COMPONENT_REVISIONS.md`.

Excluded as reproducible local data:

- full third-party source checkouts;
- downloaded cross-toolchains;
- Buildroot and compiler caches;
- Python virtual environments;
- uncompressed intermediate/output trees;
- `rpi3-sdcard.img` because it exceeds GitHub's 100 MB per-file limit; and
- `rootfs.ext2` because it exceeds GitHub's 100 MB per-file limit.

The excluded root filesystem is reproducible from `rootfs.cpio.gz`. The
original `rootfs.ext2` checksum remains recorded in both Step 5 and Step 6
manifests.

After cloning, verify the curated boot files from the repository root with:

```sh
