import pickle, pyte, re, subprocess, sys, imageio_ffmpeg
from PIL import Image, ImageDraw, ImageFont
S = "/tmp/claude-1000/-home-aslam-Minor-Project/588f201f-270c-43fa-bbae-fc64f750a545/scratchpad"
ev = pickle.load(open(S + "/run60.pkl", "rb"))
COLS = 150; CW, CH = 8, 16; W, H = 1240, 832; FPS = 15; T0 = 3.0; DUR = 60
F = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 13.29)
FB = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 13.29)
FS = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 15)
FC = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 21)
NAMES = {"black": "#2b303b", "red": "#ff6b6b", "green": "#4ade80", "brown": "#facc15", "yellow": "#facc15", "blue": "#60a5fa", "magenta": "#e879f9", "cyan": "#22d3ee", "white": "#e5e7eb"}
def col(c, d):
    if c is None or c == "default": return d
    if re.fullmatch(r"[0-9a-fA-F]{6}", c): return "#" + c
    return NAMES.get(c, d)
class Term:
    def __init__(self, rows, evs): self.rows = rows; self.scr = pyte.Screen(COLS, rows); self.st = pyte.ByteStream(self.scr); self.ev = evs; self.i = 0
    def advance(self, t):
        while self.i < len(self.ev) and self.ev[self.i][0] <= t: self.st.feed(self.ev[self.i][1]); self.i += 1
def draw_term(dr, term, x0, y0):
    dr.rectangle([x0, y0, x0 + COLS * CW + 20, y0 + term.rows * CH + 12], fill="#0b1220")
    for y in range(term.rows):
        row = term.scr.buffer[y]; x = 0
        while x < COLS:
            ch = row[x]; fg = col(ch.fg, "#cbd5e1"); bg = col(ch.bg, None)
            if ch.reverse: fg, bg = (bg or "#0b1220"), fg
            j = x
            while j < COLS and (lambda c: (col(c.fg, "#cbd5e1"), col(c.bg, None), c.reverse, c.bold) == (col(ch.fg, "#cbd5e1"), col(ch.bg, None), ch.reverse, ch.bold))(row[j]): j += 1
            txt = "".join((row[k].data or " ") for k in range(x, j))
            px = x0 + 10 + x * CW; py = y0 + 6 + y * CH
            if bg: dr.rectangle([px, py, px + (j - x) * CW, py + CH], fill=bg)
            if txt.strip(): dr.text((px, py), txt, font=FB if ch.bold else F, fill=fg)
            x = j
tx = Term(16, ev["tx"]); rx = Term(26, ev["rx"])
ff = subprocess.Popen([imageio_ffmpeg.get_ffmpeg_exe(), "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (W, H), "-r", str(FPS), "-i", "-",
                       "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "24", "-preset", "medium", "-movflags", "+faststart", S + "/how_it_works.mp4"], stdin=subprocess.PIPE)
KIND = [("DUPLICATE", "DUPLICATE (light blue): the same packet was sent twice"), ("CONFLICT", "CONFLICT (purple): same counter as before, but different data"),
        ("TAMPERED", "TAMPERED (red): one data bit changed, so the signature no longer matches"), ("REPLAY", "REPLAY (orange): an old packet was sent again, its counter is too old"),
        ("WRONG ID", "WRONG ID (pink): CAN id 0x124 is not allowed on this link"), ("GAP", "COUNTER GAP (yellow): 4 packets are missing")]
seen = set(); cap_txt = None; cap_until = -1; events = []; poster_done = False
for n in range(DUR * FPS):
    t = n / FPS; tx.advance(T0 + t); rx.advance(T0 + t)
    for y in range(5, 26):
        line = rx.scr.display[y]
        if "✖" in line:
            m = re.search(r"#(\d+)", line)
            if m and m.group(1) not in seen:
                seen.add(m.group(1))
                for k, txt in KIND:
                    if k in line: cap_txt, cap_until = txt, t + 3.4; events.append((round(t, 1), k)); break
    if t < cap_until: cap = cap_txt; hot = True
    elif t < 5: cap = "1. The SENDER (top) is the car: a new signed packet every 100 ms. Every line looks normal."; hot = False
    elif t < 11: cap = "2. The RECEIVER (bottom): Linux only carries the frames. The Secure World, on core 1, judges every one."; hot = False
    elif t > 55: cap = "Every verdict is made in the Secure World. Linux can drop frames, but it cannot hide that it did."; hot = False
    else: cap = "Green = valid. A colored line = the Secure World found an attack. The sender does not mark its attacks."; hot = False
    im = Image.new("RGB", (W, H), "#0f1b2d"); dr = ImageDraw.Draw(im)
    y = 14
    dr.rounded_rectangle([20, y, W - 20, y + 28], 8, fill="#1b5a99"); dr.text((34, y + 5), "SENDER Pi: the car (looks normal; attacks are not marked)", font=FS, fill="white"); y += 28
    draw_term(dr, tx, 20, y); y += tx.rows * CH + 12 + 8
    dr.rounded_rectangle([20, y, W - 20, y + 28], 8, fill="#9a4a0c"); dr.text((34, y + 5), "RECEIVER Pi: the Secure World's verdict, live", font=FS, fill="white"); y += 28
    draw_term(dr, rx, 20, y); y += rx.rows * CH + 12 + 8
    dr.rounded_rectangle([20, y, W - 20, y + 56], 10, fill="#f59e0b" if hot else "#162a47"); dr.text((38, y + 15), cap, font=FC, fill="#0f1b2d" if hot else "#e5eaf3")
    ff.stdin.write(im.tobytes())
    if not poster_done and len(events) >= 4 and t - (cap_until - 3.4) > 0.9 and t < cap_until:
        im.save(S + "/how_poster.png"); poster_done = True
ff.stdin.close(); ff.wait(); print("events", events, "poster", poster_done)
