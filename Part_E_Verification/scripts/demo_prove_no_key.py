#!/usr/bin/env python3
"""demo_prove_no_key.py - run on the LAPTOP during the live demo. Proves that the Linux program on the receiver which carries the CAN messages
(can_forwarder) contains NONE of the secret keys, so it cannot judge or forge a verdict: only the Secure World holds the keys.
It reads the real file from the receiver (read-only) and searches it for the three keys that are written in the Secure World source."""
import hashlib, os, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__)); PV = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(PV, "tools/monitor_client"))
import packet_gen as g                     # noqa: E402
RX = os.environ.get("RX", "10.237.27.212")
src = open(g.KEYS).read(); keys = {}
for m in re.finditer(r"#define\s+RPI3_MON_KEY_(\w+)_INIT\s*\\?\s*\{([^}]*)\}", src, re.S):
    b = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", m.group(2)))
    if len(b) == 32: keys[m.group(1)] = b
print("The Secure World source defines %d secret keys (%s), 32 bytes each." % (len(keys), ", ".join(k.lower() for k in keys)))
ok = True
for path in ("/usr/bin/can_forwarder", "/tmp/mon_client", "/tmp/canmode"):
    r = subprocess.run(["ssh", "-o", "BatchMode=yes", "root@%s" % RX, "cat %s" % path], capture_output=True)
    if r.returncode or not r.stdout: print("  %-24s (not present on the receiver)" % path); continue
    data = r.stdout; found = [k for k, v in keys.items() if v in data]
    ok &= not found
    print("  %-24s %7d bytes  sha256 %s  keys inside: %s" % (path, len(data), hashlib.sha256(data).hexdigest()[:12], ", ".join(found) if found else "NONE"))
boot = subprocess.run(["ssh", "-o", "BatchMode=yes", "root@%s" % RX, "ls /boot 2>&1 | head -1; mount | grep -c mmcblk0p1"], capture_output=True, text=True).stdout.split()
print("  boot partition (holds the Secure World image) mounted in Linux: %s" % ("NO" if boot and boot[-1] == "0" else "yes"))
print("\nRESULT: %s" % ("the program that carries the messages holds no key: it cannot judge or forge a verdict." if ok else "A KEY WAS FOUND, investigate."))
sys.exit(0 if ok else 1)
