# optee_os pre-monitor snapshot (2026-11-06)

Taken before any Component 2.2 source change. Source: `optee_full/optee_os`, HEAD in `git_head.txt`
(see git_head.txt for the hash and branch). The tree was NOT clean:
9 modified tracked files and 1 untracked file, which are the project's own earlier work.

| File | Content |
|---|---|
| `tracked_changes.diff` | `git diff --binary` of the 9 modified tracked files against HEAD |
| `files/` | a full copy of each modified file and of the untracked `rpi3_can_server.c`, original paths preserved |
| `modified_files.txt`, `untracked_files.txt`, `git_status.txt` | the lists at snapshot time |
| `shipped_out/` | `conf.mk`, `tee.bin`, `tee.elf` of the Sep-20 build that is in the running image |
| `build_repo_changes.diff`, `build_repo_untracked.tar`, `build_repo_status.txt` | the `optee_full/build` repo's own uncommitted changes (rpi3.mk, kconfigs/rpi3.conf, br-ext/board/rpi3/) |
| `SHA256SUMS` | checksums of everything above except itself |

## Verify
    cd <this dir> && sha256sum -c SHA256SUMS

## Restore the source tree (does not touch git history, no stash/reset/clean)
Either copy files back: `cp -p files/<path> <optee_os>/<path>` for each path in `modified_files.txt` and `untracked_files.txt`,
or rebuild from a clean export: `git -C <optee_os> archive HEAD | tar -x -C <dir> && cd <dir> && git apply <this dir>/tracked_changes.diff`
and copy `files/core/arch/arm/plat-rpi3/rpi3_can_server.c` in.

Read-back verified 2026-11-06: 21 of 21 checksums OK; a clean HEAD export plus `tracked_changes.diff` reproduces the live tree byte for byte (0 mismatches).
