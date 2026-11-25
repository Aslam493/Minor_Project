import os, pty, select, time, pickle, sys
RXIP, SNIP = "10.237.27.212", "10.237.27.144"
def spawn(argv):
    pid, fd = pty.fork()
    if pid == 0: os.execvp(argv[0], argv)
    return pid, fd
t0 = time.monotonic(); ev = {"rx": [], "tx": []}
rxp, rxfd = spawn(["ssh","-tt","-o","BatchMode=yes","root@"+RXIP,"stty rows 26 cols 150; /tmp/packet_view"])
txp = txfd = None; fds = {rxfd: "rx"}; done_tx = False
while time.monotonic() - t0 < 70:
    now = time.monotonic() - t0
    if txp is None and now >= 3.0:
        txp, txfd = spawn(["ssh","-tt","-o","BatchMode=yes","sender@"+SNIP,"stty rows 16 cols 150; cd /tmp/demo && python3 stream.py --duration 62"]); fds[txfd] = "tx"
    r, _, _ = select.select(list(fds), [], [], 0.05)
    for fd in r:
        try: d = os.read(fd, 65536)
        except OSError: d = b""
        if d: ev[fds[fd]].append((time.monotonic() - t0, d))
        else: fds.pop(fd, None)
os.close(rxfd)
pickle.dump(ev, open(sys.argv[1], "wb")); print({k: (len(v), v[-1][0] if v else None) for k, v in ev.items()})
