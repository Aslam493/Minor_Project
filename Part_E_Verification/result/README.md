# result/ : what is in this folder

| File | What it is |
|---|---|
| `1_PROJECT_EXPLANATION.pdf` (.md) | What we built, how it works, what we tested, component by component (C1 to C8), the problems we hit, the limits |
| `2_EVIDENCE_FILE.pdf` (.md) | The proof: a result table, then for every component what was run, what a good output looks like, and the unedited output from the laptop and from the live Raspberry Pi (collected 2026-11-18), including section C9: the packet sent by `can_sender` through the CAN controller (`can0`) and submitted by `can_forwarder` (C9), and section C10: packets made on the laptop before Linux and only replayed by the Pi (no key on the Pi) |
| `3_raw_evidence_output.txt` | The raw output the evidence file was built from (nothing edited) |

Scope: software-injected frames through the Secure World interface, not CAN-bus traffic; prototype keys; detection only, no prevention. Not done: C7 (chip B), CAN/SPI floods, the gap histogram, the stalled-UART test, the `ALIVE` line.
Regenerate: `bash scripts/collect_evidence.sh <pi-ip> <raw.txt>`, `bash scripts/canbridge_board_test.sh <pi-ip>` and `bash scripts/precreated_packets_board_test.sh <pi-ip>` (save their outputs), then `bash scripts/make_result_folder.sh <raw.txt> <canbridge_output.txt> <precreated_output.txt>`.
