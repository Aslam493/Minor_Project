#!/usr/bin/env python3
"""U0: every file path cited in backticks in the plan and the component records must exist.
usage: check_cited_paths.py   (run from anywhere; exit 1 if any cited path is unresolved and not declared as a future file)"""
import os, re, sys, glob
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PE = os.path.join(ROOT, "Part_E_Verification")
FILES = [os.path.join(ROOT, "Secure_CAN_Monitor_Plan_8_Components.md")] + \
        sorted(glob.glob(PE + "/monitor/COMPONENT_*.md")) + sorted(glob.glob(PE + "/baseline/COMPONENT_*.md")) + \
        [PE + "/monitor/NEXT_STEPS.md", PE + "/monitor/PROGRESS_REPORT_2026-11-12.md", PE + "/monitor/PLAN_REMAINING_WORK_2026-11-12.md",
         PE + "/monitor/F24_HEARTBEAT_STARVATION_ANALYSIS.md", PE + "/monitor/N04_SECURE_MEMORY_RESERVATION.md"]
FUTURE = {"Part_E_Verification/RESULTS.md", "RESULTS.md", "tools/monitor_client/c6_negative_raw.c", "tools/monitor_client/soak_watch.sh"}   # declared as not existing yet
KNOWN_ABSENT = {"Minor_Project_5_Phase_Implementation_BACKUP_2026-09-08.pptx"}   # tracked file deleted before this work; the plan calls it out as unrelated
EXT = r"(?:md|txt|c|h|sh|py|pdf|mp4|patch|json|mk|conf|img|bin|dtb|dts|dtbo|sha256|png|jpg|js|pptx)"
TOKEN = re.compile(r"`([^`\s]+?\." + EXT + r"|[^`\s]+/)`")
BASES = [ROOT, PE, PE + "/monitor", PE + "/baseline", PE + "/tools", PE + "/scripts", PE + "/monitor/source_tests", PE + "/build_scratch",
         ROOT + "/optee_full", ROOT + "/optee_full/optee_os", ROOT + "/optee_full/optee_os/core/arch/arm/plat-rpi3", ROOT + "/optee_full/optee_os/core/pta",
         ROOT + "/optee_full/build", PE + "/tools/monitor_client", PE + "/monitor/design", ROOT + "/SD_Card_Backups"]
# index of every file path in the repo and the local build trees (suffix match), built once
SKIP = {".git", "node_modules", ".venv_plots", "out-br", "buildroot", "linux", "u-boot", "optee_client", "optee_test", "optee_examples", "toolchains", "firmware", "__pycache__"}
SUFFIX = set()
for base in (ROOT, ):
    for dp, dn, fn in os.walk(base):
        dn[:] = [d for d in dn if d not in SKIP]
        for f in fn:
            parts = os.path.relpath(os.path.join(dp, f), ROOT).split(os.sep)
            for i in range(len(parts)): SUFFIX.add("/".join(parts[i:]))
# trees that are skipped for speed but cited by name (TF-A, OP-TEE core, Linux headers, firmware)
for base in (ROOT + "/optee_full/trusted-firmware-a", ROOT + "/optee_full/optee_os/core", ROOT + "/optee_full/firmware", ROOT + "/optee_full/out/boot"):
    for dp, dn, fn in os.walk(base):
        dn[:] = [d for d in dn if d not in (".git",)]
        for f in fn:
            parts = os.path.relpath(os.path.join(dp, f), ROOT).split(os.sep)
            for i in range(len(parts)): SUFFIX.add("/".join(parts[i:]))
missing = {}; seen = 0
for f in FILES:
    if not os.path.exists(f): missing.setdefault(os.path.relpath(f, ROOT), []).append("(the file itself)"); continue
    for m in TOKEN.finditer(open(f, errors="replace").read()):
        t = m.group(1).rstrip(",.;:)")
        if t.startswith(("http", "~", "/dev", "/tmp", "/proc", "/sys", "/etc", "/mnt", "--")) or "*" in t or "<" in t or "{" in t or t.startswith("$"):
            continue
        seen += 1
        if t == "/memreserve/" or "/." in t: continue          # a device-tree directive, and "x.c/.h" shorthand: not paths
        t2 = t.lstrip("./")
        if any(os.path.exists(os.path.join(b, t2)) for b in BASES) or t2.rstrip("/") in SUFFIX: continue
        if t2 in KNOWN_ABSENT: continue
        if os.path.exists(os.path.join(ROOT, "optee_full/linux/include/uapi", t2)) or os.path.exists(os.path.join(ROOT, "optee_full/linux", t2)): continue
        if t2 in FUTURE or os.path.basename(t2) in {os.path.basename(x) for x in FUTURE}: continue
        missing.setdefault(t, set()).add(os.path.relpath(f, ROOT))
print("cited paths checked:", seen, "| unresolved:", len(missing))
for t, w in sorted(missing.items()): print("  MISSING", t, "<-", ", ".join(sorted(w))[:150])
sys.exit(1 if missing else 0)
