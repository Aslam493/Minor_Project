#!/usr/bin/env python3
"""make_flow_diagram.py <out.html> : swimlane flow diagram (3 pages) of the Secure CAN Monitor: packet path, request/reply timing, Secure World verification."""
import html, sys

W, H = 1570, 1040
COL = {"lap": "#dbe7f7", "shield": "#fde5c8", "linux": "#dcf1dc", "sw": "#f6d6dc", "core1": "#e6dcf3"}
EDGE = {"lap": "#2d5a9c", "shield": "#b8650a", "linux": "#2f7a2f", "sw": "#a3263d", "core1": "#5b3a96"}
LANES = [("lap", "LAPTOP (sender ECU stand-in)"), ("shield", "CAN SHIELD (MCP2518FD)"), ("linux", "LINUX on the Pi (Normal World)"),
         ("sw", "SECURE WORLD (OP-TEE monitor)"), ("core1", "CORE 1 (Secure World loop)")]
LX = lambda i: 20 + 308 * i

def esc(t): return html.escape(t, quote=False)

class Svg:
    def __init__(self, w=W, h=H): self.w, self.h, self.p = w, h, []
    def add(self, s): self.p.append(s)
    def text(self, x, y, t, size=13, bold=False, color="#111", anchor="start", italic=False):
        self.add('<text x="%d" y="%d" font-size="%d" font-family="Helvetica,Arial,sans-serif" fill="%s" text-anchor="%s"%s%s>%s</text>' % (x, y, size, color, anchor, ' font-weight="700"' if bold else "", ' font-style="italic"' if italic else "", esc(t)))
    def box(self, x, y, w, lines, kind, num=None, h=None):
        hh = h or (16 + 17 * len(lines))
        self.add('<rect x="%d" y="%d" width="%d" height="%d" rx="8" fill="%s" stroke="%s" stroke-width="2"/>' % (x, y, w, hh, COL[kind], EDGE[kind]))
        for i, l in enumerate(lines):
            self.text(x + 10, y + 21 + 17 * i, l, 13, bold=(i == 0))
        return hh
    def arrow(self, pts, label="", dashed=False, color="#222", lx=None, ly=None, anchor="start", lw=1):
        d = " ".join("%d,%d" % p for p in pts)
        self.add('<polyline points="%s" fill="none" stroke="%s" stroke-width="2.2"%s marker-end="url(#ah)"/>' % (d, color, ' stroke-dasharray="7,5"' if dashed else ""))
        if label:
            mx, my = (lx, ly) if lx is not None else ((pts[0][0] + pts[1][0]) // 2, (pts[0][1] + pts[1][1]) // 2 - 6)
            for i, l in enumerate(label.split("\n")): self.text(mx, my + 14 * i, l, 12, color="#222", anchor=anchor, italic=True)
    def render(self):
        return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d"><defs><marker id="ah" markerWidth="10" markerHeight="8" refX="9" refY="4" orient="auto"><path d="M0,0 L10,4 L0,8 z" fill="#222"/></marker></defs>'
                '<rect width="100%%" height="100%%" fill="#ffffff"/>%s</svg>') % (self.w, self.h, self.w, self.h, "".join(self.p))

def lanes(s, top=92, bottom=H - 40):
    for i, (k, name) in enumerate(LANES):
        s.add('<rect x="%d" y="%d" width="290" height="%d" fill="%s" fill-opacity="0.35" stroke="%s" stroke-width="1.5" rx="10"/>' % (LX(i), top - 36, bottom - top + 36, COL[k], EDGE[k]))
        s.text(LX(i) + 145, top - 14, name, 14, bold=True, color=EDGE[k], anchor="middle")

def page1():
    PH = 900
    s = Svg(W, PH)
    s.text(20, 30, "Full flow: ONE way for the packet (CAN bus -> MCP2518FD -> Linux), then how Secure World asks for it and judges it (Mode P)", 20, bold=True)
    s.text(20, 52, "Status: Mode P is implemented and host-tested but NOT flashed. Heartbeat request/reply, frame checks and the path through the CAN controller are tested on the Pi.", 13, color="#a3263d", bold=True)
    lanes(s, 92, PH - 40)
    bx = lambda i: LX(i) + 15
    gap = lambda i, j: (bx(i) + 260 + bx(j)) // 2
    # the packet path, left to right
    s.box(bx(0), 105, 260, ["1  packet_gen.py makes the packets", "id | counter | payload | TAG", "TAG = HMAC-SHA256(key, first 12 bytes)", "the sender puts them on the CAN bus", "(the key stays with the sender)"], "lap")
    s.box(bx(1), 105, 260, ["2  MCP2518FD receives the frame", "from the wire, checks the CAN CRC,", "keeps it in its receive buffer,", "raises an interrupt to Linux", "(it only carries the packet; it", "does not make the data or the tag)"], "shield")
    s.arrow([(bx(0) + 260, 150), (bx(1), 150)], "CAN bus", lx=gap(0, 1), ly=142, anchor="middle")
    s.box(bx(2), 105, 260, ["3  Linux driver (mcp251xfd)", "reads the frame from the chip over SPI", "and delivers it as a received frame", "on can0 (rx_packets + 1)"], "linux")
    s.arrow([(bx(1) + 260, 150), (bx(2), 150)], "SPI", lx=gap(1, 2), ly=142, anchor="middle")
    s.box(bx(2), 245, 260, ["4  can_forwarder", "rebuilds the 44-byte message", "puts it in a QUEUE and HOLDS it", "(nothing goes to Secure World yet)"], "linux")
    s.arrow([(bx(2) + 130, 105 + 16 + 17 * 4), (bx(2) + 130, 245)], "")
    # today's shortcut, clearly separate
    s.add('<rect x="%d" y="250" width="260" height="150" rx="8" fill="#eeeeee" stroke="#777" stroke-width="2" stroke-dasharray="7,5"/>' % bx(0))
    for i, l in enumerate(["TODAY ONLY (no CAN-FD adapter)", "a shortcut for steps 1 and 2:", "the packet file is copied to the Pi", "(scp); can_replay (no key) writes", "each frame into the chip over SPI and", "the chip's loopback returns it.", "From step 3 on everything is the same."]):
        s.text(bx(0) + 10, 271 + 17 * i, l, 12, bold=(i == 0), color="#333")
    s.arrow([(bx(0) + 260, 350), (bx(1) + 130, 350), (bx(1) + 130, 105 + 16 + 17 * 6)], "today only", dashed=True, color="#777", lx=bx(0) + 270, ly=342)
    # Secure World side
    s.box(bx(4), 105, 260, ["5  Core 1 loop (about every 448 us)", "every 50 ms: post PACKET REQUEST n", "into the monitor state (arrow)", "sends nothing, never waits"], "core1")
    s.box(bx(3), 105, 260, ["Monitor state (Secure World RAM)", "request n | OPEN | issued at t", "last counter | event log | counters"], "sw")
    s.arrow([(bx(4), 140), (bx(3) + 260, 140)], "")
    s.box(bx(2), 440, 260, ["6  can_forwarder asks:", "CALL command 30", "'get packet request'"], "linux")
    s.box(bx(3), 440, 260, ["Command 30 (PTA)", "returns: request n, OPEN,", "max batch 32, due in 40 ms"], "sw")
    s.arrow([(bx(2) + 260, 475), (bx(3), 475)], "SMC", lx=gap(2, 3), ly=467, anchor="middle")
    s.arrow([(bx(3), 505), (bx(2) + 260, 505)], "returns", dashed=True, lx=gap(2, 3), ly=522, anchor="middle")
    s.box(bx(2), 570, 260, ["7  takes the oldest (up to 32)", "packets from the queue", "CALL command 31", "(request n, packets)"], "linux")
    hv = s.box(bx(3), 560, 260, ["8  VERIFY in Secure World", "a. request n: ON_TIME / LATE /", "   STALE / UNISSUED / DUPLICATE", "b. each packet: length 44, id 0x123", "c. copy once, HMAC(key) outside lock", "d. tag equal? no = AUTH_FAIL", "e. counter: RESYNC / ACCEPTED / GAP /", "   REPLAY / DUPLICATE / CONFLICT", "f. valid = refresh 300 ms deadline", "   and record the event"], "sw")
    s.arrow([(bx(2) + 260, 610), (bx(3), 610)], "SMC +\npackets", lx=gap(2, 3), ly=586, anchor="middle")
    s.box(bx(2), 760, 260, ["9  reply: request verdict +", "number of packets judged", "forwarder removes judged packets", "from the queue; the rest wait"], "linux")
    s.arrow([(bx(3), 560 + hv - 22), (gap(2, 3), 560 + hv - 22), (gap(2, 3), 790), (bx(2) + 260, 790)], "", dashed=True)
    s.box(bx(4), 440, 260, ["10  by the clock (reads the state)", "no reply within 40 ms: MISSED,", "   miss count + 1", "3 misses in a row: ONE TIMEOUT", "   (detail 4)", "no valid packet for 300 ms:", "   traffic TIMEOUT (detail 1)"], "core1")
    s.arrow([(bx(4), 475), (bx(3) + 260, 475)], "")
    s.text(20, PH - 12, "Solid arrow = a call or data going forward. Dashed = the value coming back, or today's shortcut. SMC = the call into Secure World (libteec, /dev/tee0, OP-TEE driver, TF-A at EL3, OP-TEE). Every crossing between Linux and Secure World is a call that LINUX starts.", 12, color="#444")
    return s.render()

def page2():
    s = Svg(W, 900)
    s.text(20, 30, "Timing: when Secure World asks and when a reply is on time, late or missing", 20, bold=True)
    def panel(y0, title, period, due, unit_px, note, tail):
        s.text(20, y0, title, 16, bold=True)
        base = y0 + 40; ax = 140
        s.text(20, base + 38, "Secure World", 13, bold=True, color=EDGE["core1"]); s.text(20, base + 118, "Linux", 13, bold=True, color=EDGE["linux"])
        s.add('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="#555" stroke-width="2"/>' % (ax, base + 130 + 40, ax + unit_px * period * 3 + 60, base + 130 + 40))
        for k in range(4):
            x = ax + k * unit_px * period
            s.add('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="#999" stroke-dasharray="3,4"/>' % (x, base, x, base + 170))
            s.text(x, base + 190, "%d ms" % (k * period), 12, anchor="middle")
            if k < 3:
                s.box(x + 4, base + 8, 150, ["request %s posted (OPEN)" % ("n" if k == 0 else "n+%d" % k)], "core1", h=30)
                s.add('<rect x="%d" y="%d" width="%d" height="14" fill="#f6d6dc" stroke="#a3263d"/>' % (x, base + 58, unit_px * due))
                s.text(x + 2, base + 70, "reply due within %d ms" % due, 11)
        x0 = ax
        s.box(x0 + 12, base + 96, 330, ["Linux fetches the request, replies in time", "= ON_TIME  (miss count reset)"], "linux", h=44)
        s.arrow([(x0 + 40, base + 96), (x0 + 40, base + 40)], "", color="#2f7a2f")
        x1 = ax + unit_px * period
        s.box(x1 + 12, base + 96, 330, ["No reply by the due time: request MISSED,", "miss count + 1. A late reply is judged but", "logged LATE_ANSWER; the miss stays"], "linux", h=60)
        x2 = ax + 2 * unit_px * period
        s.box(x2 + 12, base + 96, 330, tail, "sw", h=60)
        s.text(20, base + 215, note, 13, color="#333")
    panel(70, "A. Heartbeat (built and tested on the Pi): one request every 1000 ms, reply due within 500 ms", 1000, 500, 0.33,
          "Constants: H = 1000 ms, D_req = 500 ms, K = 3 misses, start-up grace 60 s (rpi3_can_monitor.h). The reply carries the request number and a nonce.",
          ["3 misses in a row (or never answered", "after the 60 s grace): ONE heartbeat TIMEOUT", "(detail 2 or 3); an on-time reply recovers"])
    panel(390, "B. Packet request (Mode P, host-tested, not flashed): one request every 50 ms, reply due within 40 ms", 50, 40, 6.6,
          "Constants: P = 50 ms, D_p = 40 ms, up to 32 packets per reply, K = 3, grace 60 s. An EMPTY reply is valid (Linux alive, sender silent).",
          ["3 misses in a row: ONE TIMEOUT (detail 4)", "Separately: no valid packet for 300 ms", "= traffic TIMEOUT (detail 1)"])
    y = 720
    s.text(20, y, "C. Why 300 ms for traffic:  request period + reply time + sender period + jitter  =  50 + 40 + 100 + 50 = 240 ms  <  300 ms (checked when the code is compiled), so a healthy system raises no false alarm.", 14, bold=True)
    s.text(20, y + 28, "D. Invalid, late and missing are three different things:", 14, bold=True)
    rows = [("INVALID content", "bad tag, wrong id or length, wrong counter, reply to the wrong request", "AUTH_FAIL, MALFORMED, REPLAY, DUPLICATE, GAP, STALE, UNISSUED"),
            ("LATE", "the right reply, but after the due time", "request already MISSED (miss + 1); reply judged, logged LATE_ANSWER"),
            ("MISSING", "no reply at all in time", "miss count + 1 per request; 3 in a row = ONE TIMEOUT")]
    for i, (a, b, c) in enumerate(rows):
        yy = y + 50 + 40 * i
        s.add('<rect x="20" y="%d" width="1460" height="34" fill="#f4f4f4" stroke="#999"/>' % yy)
        s.text(30, yy + 22, a, 13, bold=True); s.text(190, yy + 22, b, 13); s.text(790, yy + 22, c, 13)
    return s.render()

def page3():
    s = Svg(W, 1040)
    s.text(20, 30, "How Secure World verifies a reply (command 31 in Mode P; the packet checks are the same as command 17 in the built design)", 20, bold=True)
    steps = [
        ("start", "Command 31 arrives: request number n, count k, k x 44 bytes", "sw"),
        ("dec", "k <= 32 and length = 44 x k ?   no: BAD_PARAMETERS, nothing changes", "sw"),
        ("dec", "First hold: judge the REQUEST n (same order as the heartbeat answer)", "sw"),
        ("dec", "UNISSUED / STALE / DUPLICATE  ->  stop. No packet is judged. Event logged.", "sw"),
        ("proc", "ON_TIME (state ANSWERED, miss count 0) or LATE (logged, miss stays)  ->  go on", "sw"),
        ("loop", "FOR EACH packet (at most 32):", "core1"),
        ("proc", "1  copy the 44 bytes once, byte by byte, before any lock", "sw"),
        ("dec", "2  length is 44 and CAN id is 0x123 ?   no: MALFORMED", "sw"),
        ("proc", "3  HMAC-SHA256(key, id | counter | payload) OUTSIDE the lock (BUSY here changes nothing)", "sw"),
        ("dec", "4  constant-time compare with the 32 tag bytes   different: AUTH_FAIL (deadline not refreshed)", "sw"),
        ("proc", "5  counter rules, one short lock hold:  first = RESYNC | next = ACCEPTED | jump = GAP | old = REPLAY | same = DUPLICATE | same counter, other content = CONFLICT", "sw"),
        ("proc", "6  RESYNC, ACCEPTED, GAP refresh the 300 ms traffic deadline (a recovery event if it had expired); the others do not", "sw"),
        ("proc", "7  record the event (first 4 of each evidence class kept; others limited to 1 per second per class; counters stay exact)", "sw"),
        ("end", "Reply to Linux: request verdict + how many packets were judged. BUSY on a later packet stops the batch; the rest stay queued.", "linux"),
    ]
    y = 60
    for idx, (kind, t, k) in enumerate(steps):
        h = 46
        if kind == "dec":
            s.add('<rect x="30" y="%d" width="1450" height="%d" rx="8" fill="#fff3c4" stroke="#b58900" stroke-width="2.5"/>' % (y, h)); s.text(44, y + 29, "CHECK", 12, bold=True, color="#7a5c00")
        else:
            s.add('<rect x="30" y="%d" width="1450" height="%d" rx="%d" fill="%s" stroke="%s" stroke-width="2"/>' % (y, h, 20 if kind in ("start", "end") else 8, COL[k], EDGE[k]))
        s.text(750, y + 25, t, 14, bold=(kind in ("start", "end", "loop")), anchor="middle")
        if idx < len(steps) - 1: s.arrow([(750, y + h), (750, y + h + 16)], "")
        y += h + 16
    s.text(20, 1010, "Yellow = a check that can reject. Time is read from the Secure World timer (19.2 MHz), which Linux cannot change. The key never leaves Secure World and the sender; Linux only relays bytes.", 12, color="#444")
    return s.render()

def main():
    out = sys.argv[1]
    pages = [page1(), page2(), page3()]
    body = "".join('<div class="pg">%s</div>' % p for p in pages)
    open(out, "w").write('<!doctype html><html><head><meta charset="utf-8"><title>Secure CAN Monitor flow diagram</title><style>@page{size:%dpx %dpx;margin:0}html,body{margin:0;padding:0}.pg{width:%dpx;height:%dpx;page-break-after:always;overflow:hidden}svg{display:block}</style></head><body>%s</body></html>' % (W, H, W, H, body))
main()
