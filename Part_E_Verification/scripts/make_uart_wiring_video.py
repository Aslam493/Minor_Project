#!/usr/bin/env python3
"""Build UART_wiring_howto.mp4: narrated animation of the FTDI -> Pi UART hookup.

Run with the project venv (it has pillow, piper-tts, imageio-ffmpeg):
  .venv_plots/bin/python Part_E_Verification/scripts/make_uart_wiring_video.py <out.mp4> <workdir>
"""
import math, os, subprocess, sys, wave, glob
from PIL import Image, ImageDraw, ImageFont
import imageio_ffmpeg

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT, WORK = sys.argv[1], sys.argv[2]
os.makedirs(WORK, exist_ok=True)
FFMPEG = imageio_ffmpeg.get_ffmpeg_exe()
VOICE = os.path.join(ROOT, "video_build/voice/en_US-lessac-medium.onnx")
PHOTO = os.path.join(ROOT, "Part_E_Verification/scripts/assets/header_crop.jpg")
W, H, FPS = 1280, 720, 15

F = "/usr/share/fonts/truetype/dejavu/"
def font(sz, bold=False, mono=False):
    n = "DejaVuSansMono.ttf" if mono else ("DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf")
    return ImageFont.truetype(F + n, sz)

BG = (18, 24, 38); FG = (235, 240, 250); DIM = (140, 152, 175)
GREEN = (80, 220, 130); YEL = (255, 210, 70); RED = (255, 85, 85); BLUE = (90, 170, 255)
BOARD = (30, 110, 70); BLACK = (10, 10, 10)

NARR = [
 # (key, text)
 ("s1", "This video shows how to connect a USB serial adapter to the Raspberry Pi, "
        "through the top header of your CAN shield. Before you start, switch the Pi off, "
        "and set the voltage jumper on the adapter to three point three volts."),
 ("s2", "The shield has a pass through header on top. Find the triangle. That marks pin one, "
        "in the left column. The left column holds the odd pins, the right column holds the even pins. "
        "Count down the right column. Pin six is ground. Pin eight is transmit. Pin ten is receive."),
 ("s3", "Now the wiring. Ground goes to ground. The Pi's transmit, pin eight, goes to the adapter's receive. "
        "The Pi's receive, pin ten, goes to the adapter's transmit. The two data wires cross over. "
        "Never connect the five volt pin."),
 ("s4", "Your jumper wires have female ends, and the header is also female, so they cannot plug in directly. "
        "Use male to female jumper wires. Or push three male header pins into the sockets, "
        "and put your female jumpers on top of them."),
 ("s5", "Plug the adapter into the laptop, and open a terminal. Run picocom, at one one five two zero zero baud. "
        "Then power on the Pi. If you see nothing, swap the transmit and receive wires first."),
]

def tts(key, text):
    wav = os.path.join(WORK, key + ".wav")
    if not os.path.exists(wav):
        subprocess.run([sys.executable, "-m", "piper", "-m", VOICE, "-f", wav],
                       input=text.encode(), check=True, stderr=subprocess.DEVNULL)
    with wave.open(wav) as w:
        return wav, w.getnframes() / w.getframerate()

def caption(d, text, y=655):
    d.rectangle([0, y - 8, W, H], fill=(10, 14, 24))
    d.text((W // 2, y + 24), text, font=font(24), fill=FG, anchor="mm")

def title(d, text, sub=None):
    d.text((50, 38), text, font=font(36, True), fill=FG)
    if sub: d.text((50, 88), sub, font=font(21), fill=DIM)

def ease(t): t = max(0, min(1, t)); return t * t * (3 - 2 * t)

# ---------------- scenes ----------------
def scene1(t, dur):
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    d.text((W // 2, 170), "UART hookup for the Secure CAN project", font=font(44, True), fill=FG, anchor="mm")
    d.text((W // 2, 235), "USB serial adapter  →  Pi header (through the CAN shield)", font=font(26), fill=BLUE, anchor="mm")
    items = [("1", "Power the Pi OFF", RED), ("2", "Adapter voltage jumper = 3.3 V (not 5 V)", YEL),
             ("3", "Wire GND, TX, RX only", GREEN)]
    for i, (n, s, c) in enumerate(items):
        a = ease((t - 1.2 - i * 2.0) / 0.6)
        if a <= 0: continue
        y = 340 + i * 80
        col = tuple(int(BG[k] + (c[k] - BG[k]) * a) for k in range(3))
        d.ellipse([300, y - 26, 352, y + 26], outline=col, width=4)
        d.text((326, y), n, font=font(28, True), fill=col, anchor="mm")
        d.text((380, y), s, font=font(30), fill=tuple(int(BG[k] + (FG[k] - BG[k]) * a) for k in range(3)), anchor="lm")
    return im

PIN_TOP = (170, 154); STEP = 69.5; COLX = {"L": 170, "R": 242}
def pin_xy(n):  # physical pin number -> crop coords
    row = (n + 1) // 2 - 1
    return (COLX["L"] if n % 2 else COLX["R"], PIN_TOP[1] + STEP * row)

def scene2(t, dur):
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    title(d, "Find pins 6, 8 and 10", "top (pass-through) header of the shield, triangle end = pin 1")
    ph = Image.open(PHOTO).convert("RGB").resize((560, 840))
    sc = 1.4
    px, py = 80, -20
    im.paste(ph.crop((0, 90, 560, 750)), (px, 110))
    off = (px, 110 - 90)
    def P(n):
        x, y = pin_xy(n); return (off[0] + x * sc, off[1] + y * sc)
    def ring(n, c, a=1.0, r=26):
        x, y = P(n)
        rr = r + 5 * math.sin(t * 6)
        d.ellipse([x - rr, y - rr, x + rr, y + rr], outline=c, width=5)
    # pin 1
    if t > 0.8:
        ring(1, BLUE)
        x, y = P(1); d.text((x - 60, y), "pin 1", font=font(24, True), fill=BLUE, anchor="rm")
    # count down right column
    labels = {2: "2", 4: "4", 6: "6  GND", 8: "8  TXD (GPIO14)", 10: "10  RXD (GPIO15)"}
    cols = {6: (150, 150, 150), 8: GREEN, 10: YEL}
    for k, n in enumerate([2, 4, 6, 8, 10]):
        if t > 3.5 + k * 1.1:
            x, y = P(n)
            if n in cols:
                ring(n, cols[n])
                d.line([x + 34, y, 520, y], fill=cols[n], width=3)
                d.text((530, y), labels[n], font=font(28, True), fill=cols[n], anchor="lm")
            else:
                d.text((x + 38, y), labels[n], font=font(20), fill=DIM, anchor="lm")
    d.text((680, 560), "left column = odd pins\nright column = even pins", font=font(22), fill=DIM, spacing=8)
    caption(d, "Count down the RIGHT column from the triangle: 2, 4, 6, 8, 10")
    return im

def box(d, xy, text, color):
    d.rounded_rectangle(xy, 14, fill=(28, 36, 56), outline=color, width=3)
    d.text(((xy[0] + xy[2]) // 2, xy[1] + 28), text, font=font(24, True), fill=color, anchor="mm")

def scene3(t, dur):
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    title(d, "Wiring: 3 wires only", "Pi side = shield header pins; adapter side = USB serial board")
    box(d, (90, 150, 420, 600), "USB serial adapter", BLUE)
    box(d, (860, 150, 1190, 600), "Pi (shield header)", GREEN)
    ad = {"GND": 230, "RX": 330, "TX": 430, "5V": 530}
    pi = {"GND": ("pin 6  GND", 230), "TXD": ("pin 8  TXD", 330), "RXD": ("pin 10  RXD", 430)}
    for k, y in ad.items():
        c = RED if k == "5V" else FG
        d.ellipse([408, y - 9, 426, y + 9], fill=c)
        d.text((395, y), k, font=font(26, True), fill=c, anchor="rm")
    for k, (lab, y) in pi.items():
        d.ellipse([854, y - 9, 872, y + 9], fill=FG)
        d.text((885, y), lab, font=font(26, True), fill=FG, anchor="lm")
    wires = [("GND", "GND", 230, 230, (170, 170, 170), 1.5, "GND to GND"),
             ("TX", "RXD", 430, 430, YEL, 4.5, "adapter TX  →  Pi RXD (pin 10)"),
             ("RX", "TXD", 330, 330, GREEN, 8.0, "adapter RX  ←  Pi TXD (pin 8)")]
    # straight wires, drawn in order
    for _, _, y1, y2, c, t0, lab in wires:
        a = ease((t - t0) / 1.5)
        if a > 0:
            x2 = 426 + (854 - 426) * a
            d.line([426, y1, x2, y2], fill=c, width=7)
            if a >= 1: d.text((640, y1 - 22), lab, font=font(21, True), fill=c, anchor="mm")
    if t > 12:
        d.text((640, 585), "5V: leave unconnected", font=font(24, True), fill=RED, anchor="mm")
    caption(d, "Pi TX goes to adapter RX, Pi RX goes to adapter TX: the data wires cross over")
    return im

def scene4(t, dur):
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    title(d, "Female header + female jumpers do not mate", "pick one adapter method")
    def socket(cx, y):
        d.rounded_rectangle([cx - 45, y, cx + 45, y + 90], 6, fill=BLACK, outline=(90, 90, 90), width=2)
        d.rectangle([cx - 11, y + 8, cx + 11, y + 34], fill=(70, 70, 70))
        d.text((cx, y + 112), "socket on shield header", font=font(18), fill=DIM, anchor="mm")
    def housing(cx, ytop, label=None):
        d.rounded_rectangle([cx - 22, ytop, cx + 22, ytop + 70], 4, fill=(35, 35, 35), outline=(120, 120, 120), width=2)
        d.rectangle([cx - 9, ytop + 50, cx + 9, ytop + 70], fill=(10, 10, 10))
    def pin(cx, ytop, ln=100):
        d.rectangle([cx - 5, ytop, cx + 5, ytop + ln], fill=(215, 185, 80))
    def wire(cx, y0):
        d.line([cx, y0, cx, y0 - 110], fill=(60, 60, 60), width=8)
    CA, CB, SY = 320, 960, 440
    # A: M-F jumper
    d.text((CA, 175), "A   Male-to-female jumper wire", font=font(26, True), fill=GREEN, anchor="mm")
    socket(CA, SY)
    a = ease((t - 1.5) / 2.5)
    ytop = 230 + (SY - 100 - 230) * a   # male pin tip reaches socket
    wire(CA, ytop)
    housing(CA, ytop - 0 if False else ytop)
    pin(CA, ytop + 70, 100)
    d.text((CA, 595), "male end into the header, female end onto the adapter", font=font(20), fill=DIM, anchor="mm")
    d.line([640, 250, 640, 560], fill=(50, 60, 80), width=2)
    # B: header pin strip + female jumpers
    d.text((CB, 175), "B   3 loose header pins + your jumpers", font=font(26, True), fill=YEL, anchor="mm")
    socket(CB, SY)
    b = ease((t - 5.5) / 2.5)
    ptop = 250 + (SY - 40 - 250) * b
    pin(CB, ptop, 130)
    c = ease((t - 9.0) / 2.5)
    htop = 150 + 0
    if t > 9.0:
        hy = 215 + (SY - 150 - 215 + 0) * 0 + 0
        hy = 190 + (ptop - 130 - 190 + 0 + 0) * c if False else 215 + 20 * c
        housing(CB, hy)
        d.line([CB, hy, CB, hy - 40], fill=(60, 60, 60), width=8)
        d.text((CB, 595), "female jumper pushes onto the pin sticking up", font=font(20), fill=DIM, anchor="mm")
    else:
        d.text((CB, 595), "push the short end of each pin into the socket", font=font(20), fill=DIM, anchor="mm")
    caption(d, "Make sure no two pins touch, and keep wires away from the CAN screw terminals")
    return im

def scene5(t, dur):
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    title(d, "Open the serial console on the laptop", "115200 baud, 8N1, no flow control")
    d.rounded_rectangle((120, 150, 1160, 520), 12, fill=(8, 10, 14), outline=(70, 80, 100), width=2)
    cmd = "$ picocom -b 115200 /dev/ttyUSB0"
    n = int(max(0, t - 1.0) * 14)
    d.text((150, 190), cmd[:n], font=font(30, mono=True), fill=GREEN)
    if t > 5.0:
        d.text((150, 270), "(now power on the Pi: boot messages should appear)", font=font(22), fill=DIM)
    if t > 8.0:
        d.text((150, 340), "No output?", font=font(28, True), fill=YEL)
        d.text((150, 390), "1. swap the TX and RX wires", font=font(26), fill=FG)
        d.text((150, 435), "2. check GND and the 3.3 V jumper", font=font(26), fill=FG)
    d.text((W // 2, 570), "Exit picocom:  Ctrl-A  then  Ctrl-X", font=font(24), fill=DIM, anchor="mm")
    caption(d, "The boot log from TF-A, OP-TEE and Linux shares this one UART")
    return im

SCENES = {"s1": scene1, "s2": scene2, "s3": scene3, "s4": scene4, "s5": scene5}

def main():
    parts = []
    for key, text in NARR:
        wav, dur = tts(key, text)
        dur += 1.2  # pad after narration
        nfr = int(dur * FPS)
        fdir = os.path.join(WORK, "f_" + key); os.makedirs(fdir, exist_ok=True)
        for f in glob.glob(fdir + "/*.png"): os.remove(f)
        for i in range(nfr):
            SCENES[key](i / FPS, dur).save(f"{fdir}/{i:05d}.png")
        seg = os.path.join(WORK, key + ".mp4")
        subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-framerate", str(FPS), "-i", f"{fdir}/%05d.png",
                        "-i", wav, "-af", "apad", "-t", f"{dur:.2f}", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                        "-c:a", "aac", "-shortest", seg], check=True)
        parts.append(seg); print(key, f"{dur:.1f}s")
    lst = os.path.join(WORK, "list.txt")
    open(lst, "w").write("".join(f"file '{p}'\n" for p in parts))
    subprocess.run([FFMPEG, "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", lst,
                    "-c", "copy", OUT], check=True)
    print("wrote", OUT)

if __name__ == "__main__":
    main()
