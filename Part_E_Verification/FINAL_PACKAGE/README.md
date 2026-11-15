# Final package (copies; the originals stay where the documents cite them)

Secure CAN Monitor, detection only, Raspberry Pi 3 B, OP-TEE. Read `1_Reports/RESULTS.pdf` first: it states the limits.
All frames are software-injected through the PTA, not CAN-bus traffic; keys are prototype values; no prevention.

| Folder | What is in it |
|---|---|
| `1_Reports` | RESULTS (soaks, detections, limits), today's check report, full report (work done and issues), progress report, C7 (designed, not built) |
| `2_Test_Results` | on-Pi full check (48 PASS, 0 FAIL, 1 KNOWN, 1 SKIP) and its first run (3 script defects), C5 board tests (26 PASS), C6 negative tests, soak analysis, live demo output, 21-boot ID table, mutation re-run |
| `3_Logs` | UART boot log, soak CSV (7.27 h), soak events, soak 1 summary, F24 flood runs |
| `4_Scripts` | the on-Pi check, the live demo, the reboot sample, the soak analyser, the UART capture |

Repo: branch `nov-2026-backup`. Off-laptop backup: 4 GB card labelled PIBACKUP (SD images, source bundles, repo bundle).
Rollback image: hardened c16384, sha256 `775ce3849a38...`, in `SD_Card_Backups/SHA256SUMS`.
