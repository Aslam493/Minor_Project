#!/usr/bin/env python3
"""demo_cores.py - run on the LAPTOP during the live demo. Reads the receiver (read-only) and prints ONE table: for each of the 4 CPU cores of the Pi 3,
who owns it (Linux or the Secure World), what runs on it, and the proof. Nothing is changed on the Pi.
  python3 scripts/demo_cores.py        (RX=<receiver ip> if the address changed)"""
import os, re, subprocess, sys, time
RX = os.environ.get("RX", "10.237.27.212")


def rx(cmd, t=60):
    return subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=6", "root@%s" % RX, cmd], capture_output=True, text=True, timeout=t).stdout


def expand(s):
    out = set()
    for part in s.strip().split(","):
        if "-" in part:
            a, b = part.split("-"); out |= set(range(int(a), int(b) + 1))
        elif part.strip().isdigit(): out.add(int(part))
    return out


PV = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if "ok" not in rx("test -x /tmp/mon_client && echo ok"):          # the tools live in RAM and are wiped by every reboot: put the one we need back
    subprocess.run(["scp", "-q", "-o", "BatchMode=yes", os.path.join(PV, "tools/monitor_client/mon_client"), "root@%s:/tmp/" % RX], timeout=60)
    rx("chmod +x /tmp/mon_client")
raw = rx('echo "##uptime $(cut -d\" \" -f1 /proc/uptime)"; echo "##online $(cat /sys/devices/system/cpu/online)"; echo "##offline $(cat /sys/devices/system/cpu/offline)"; '
         'echo "##dmesg"; dmesg | grep -i "CPU1: failed to come online" | head -1; '
         'echo "##stat1"; grep ^cpu /proc/stat; sleep 1; echo "##stat2"; grep ^cpu /proc/stat; '
         'echo "##irq"; grep -E "^ *CPU|spi0.0|3f204000.spi" /proc/interrupts; '
         'echo "##procs"; for f in /proc/[0-9]*/stat; do cat $f 2>/dev/null; done; '
         'echo "##core1a"; /tmp/mon_client --retries 20 status 0 | grep -o "poll_count [0-9]*"; sleep 1; echo "##core1b"; /tmp/mon_client --retries 20 status 0 | grep -o "poll_count [0-9]*"')
if not raw.strip():
    print("The receiver does not answer at %s (MAC b8:27:eb:5c:ba:a9). Check power and the address, or run with RX=<ip>." % RX); sys.exit(2)
sec = {}; cur = None
for line in raw.splitlines():
    if line.startswith("##"):
        cur = line[2:].split()[0]; sec[cur] = [line[2:].split(None, 1)[1]] if " " in line[2:] else []
    elif cur: sec[cur].append(line)
online = expand(" ".join(sec.get("online", ["0-3"])).strip()); offline = expand(" ".join(sec.get("offline", [""])).strip())
dm = " ".join(sec.get("dmesg", [])).strip()


def cpustat(lines):
    d = {}
    for l in lines:
        m = re.match(r"cpu(\d+) (.*)", l)
        if m:
            v = [int(x) for x in m.group(2).split()]; d[int(m.group(1))] = (sum(v), v[3] + (v[4] if len(v) > 4 else 0))
    return d


s1, s2 = cpustat(sec.get("stat1", [])), cpustat(sec.get("stat2", []))
busy = {c: (100.0 * (1 - (s2[c][1] - s1[c][1]) / max(1, s2[c][0] - s1[c][0]))) for c in s1 if c in s2}
irq = {}
hdr = None
for l in sec.get("irq", []):
    if l.strip().startswith("CPU"): hdr = [int(x[3:]) for x in l.split()]
    elif hdr and ("spi0.0" in l or "3f204000.spi" in l):
        nums = re.findall(r"^\s*\d+:\s+((?:\d+\s+)+)", l)
        name = "CAN chip pin (spi0.0)" if "spi0.0" in l else "SPI controller"
        if nums:
            vals = [int(x) for x in nums[0].split()][:len(hdr)]
            for c, v in zip(hdr, vals): irq.setdefault(c, {})[name] = v
progs = {}
for l in sec.get("procs", []):
    m = re.match(r"^(\d+) \((.*)\) (\S) (.*)$", l)
    if not m: continue
    rest = m.group(4).split()
    try: ppid = int(rest[0]); cpu = int(rest[36])
    except (IndexError, ValueError): continue
    if int(m.group(1)) <= 2 or ppid in (0, 2): continue
    progs.setdefault(cpu, []).append(m.group(2))
interesting = ("can_forwarder", "tee-supplicant", "sshd", "dropbear", "syslogd", "klogd", "crond", "dhcpcd", "sh", "ash", "mon_client")
def names(c):
    p = [n for n in progs.get(c, [])]
    pick = sorted(set(n for n in p if any(n.startswith(i) for i in interesting)))
    return ", ".join(pick[:5]) if pick else "(no user program at this moment)"


def pc(sec_name):
    m = re.search(r"poll_count (\d+)", " ".join(sec.get(sec_name, [])))
    return int(m.group(1)) if m else None


a, b = pc("core1a"), pc("core1b")
rate = (b - a) if (a is not None and b is not None) else None
print("\nCPU CORES OF THE RECEIVER (Raspberry Pi 3 has 4 cores)\n")
print("Receiver uptime: %s s" % " ".join(sec.get("uptime", ["?"])).strip())
print("Linux says: online = %s, offline = %s" % (",".join(map(str, sorted(online))) or "-", ",".join(map(str, sorted(offline))) or "-"))
if dm: print("Boot log : %s   (Linux asked for core 1 and was refused)" % dm.split("] ", 1)[-1])
print("\n%-5s %-14s %-9s %-52s %s" % ("CORE", "OWNER", "LINUX SEES", "WHAT RUNS ON IT", "PROOF"))
print("-" * 120)
for c in range(4):
    if c in online:
        extra = ""
        if c in irq: extra = "; CAN-chip interrupts handled on this core: %d" % irq[c].get("CAN chip pin (spi0.0)", 0)
        print("%-5d %-14s %-9s %-52s %s" % (c, "Linux", "online", names(c)[:52], ("busy %.0f%%" % busy[c] if c in busy else "") + extra))
    else:
        print("%-5d %-14s %-9s %-52s %s" % (c, "SECURE WORLD", "offline", "the JUDGE: checks every message, heartbeat, timeouts",
              ("its own loop counter grew by %s in 1 s (Linux cannot see this core)" % format(rate, ",")) if rate else "its loop counter is read through /dev/tee0"))
print("\n(The CAN-chip interrupt counter grows when messages arrive: send a file and run this again to see it change.)")
print("\nIn words: cores 0, 2 and 3 run Linux (the postman and all normal programs; Linux moves programs between them).")
print("Core 1 is reserved for the Secure World: Linux cannot use it, and the number above comes from the Secure World itself.")
