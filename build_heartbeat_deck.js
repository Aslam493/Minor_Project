const pptxgen = require("pptxgenjs");

const LOGO = "/home/aslam/Minor_Project/midterm_deck_v2/html/IIT_Delhi_logo.png";

// ---- palette (same as other decks) ----
const NAVY = "0B1F3A";
const BLUE = "2A78D6";
const BLUE_LT = "5FA8FF";
const GREEN = "1E8E3E";
const GREEN_BG = "E6F4EA";
const RED = "C23B3B";
const RED_BG = "FBE8E8";
const ORANGE = "B8560E";
const ORANGE_BG = "FDECD9";
const GREY = "8A8F98";
const GREY_BG = "EDEEF0";
const INK = "141821";
const SUBTLE = "5B6270";
const CARD_BORDER = "E3E5EA";
const BG_LIGHT = "F7F8FA";
const FONT = "Arial";

function pres() {
  const p = new pptxgen();
  p.layout = "LAYOUT_WIDE";
  return p;
}
function addLogo(slide, opts = {}) {
  slide.addImage({ path: LOGO, x: opts.x ?? 12.55, y: opts.y ?? 0.28, w: 0.55, h: 0.55 });
}
function addFooter(slide, pageNum, total, dark) {
  const color = dark ? "6D7D96" : "8A8F98";
  slide.addText("Secure World ↔ Linux Heartbeat Liveness Design", {
    x: 0.5, y: 7.12, w: 7, h: 0.3, fontFace: FONT, fontSize: 10, color, isTextBox: true, margin: 0,
  });
  slide.addText(`${pageNum} / ${total}`, {
    x: 12.0, y: 7.12, w: 0.9, h: 0.3, fontFace: FONT, fontSize: 10, color, align: "right", isTextBox: true, margin: 0,
  });
}
function addHeader(slide, kicker, title, subtitle, kickerColor = BLUE) {
  slide.addText(kicker.toUpperCase(), {
    x: 0.5, y: 0.32, w: 10, h: 0.3, fontFace: FONT, fontSize: 13, bold: true, color: kickerColor, charSpacing: 2, isTextBox: true, margin: 0,
  });
  slide.addShape("line", { x: 0.5, y: 0.62, w: 11.4, h: 0, line: { color: NAVY, width: 1.2 } });
  slide.addText(title, { x: 0.5, y: 0.74, w: 11.8, h: 0.62, fontFace: FONT, fontSize: 27, bold: true, color: NAVY, isTextBox: true, margin: 0 });
  if (subtitle) {
    slide.addText(subtitle, { x: 0.5, y: 1.3, w: 11.8, h: 0.4, fontFace: FONT, fontSize: 13, color: SUBTLE, isTextBox: true, margin: 0 });
  }
}
function bulletBox(slide, items, opts) {
  const { x, y, w, h, fontSize = 13, color = INK, lineSpacingMultiple = 1.25 } = opts;
  slide.addText(items.map((t) => ({ text: t, options: { breakLine: true } })), {
    x, y, w, h, fontFace: FONT, fontSize, color, isTextBox: true, bullet: { code: "2022", indent: 18 }, lineSpacingMultiple, valign: "top", margin: 0,
  });
}
function card(slide, { x, y, w, h, fill = "FFFFFF", border = CARD_BORDER, radius = 0.06 }) {
  slide.addShape("roundRect", { x, y, w, h, rectRadius: radius, fill: { color: fill }, line: { color: border, width: 1 } });
}
function box(slide, text, { x, y, w, h, fill, border, color = "FFFFFF", fontSize = 12, bold = true }) {
  slide.addShape("roundRect", { x, y, w, h, rectRadius: 0.07, fill: { color: fill }, line: { color: border || fill, width: 1 } });
  slide.addText(text, { x, y, w, h, align: "center", valign: "middle", fontFace: FONT, fontSize, bold, color, isTextBox: true, margin: 2 });
}
function arrow(slide, { x, y, w = 0, h = 0, color = NAVY, label }) {
  slide.addShape("line", { x, y, w, h, line: { color, width: 2, endArrowType: "triangle" } });
  if (label) {
    slide.addText(label, { x: x - 0.3, y: y - 0.32, w: Math.max(w, 1.6), h: 0.3, align: "center", fontFace: FONT, fontSize: 10, italic: true, color: SUBTLE });
  }
}

const TOTAL = 8;
const p = pres();

// ============================================================ SLIDE 1 — TITLE
{
  const s = p.addSlide();
  s.background = { color: "F2F3F5" };
  s.addText("SECURE CAN GATEWAY  |  DESIGN ADDITION", {
    x: 0.7, y: 1.9, w: 10, h: 0.35, fontFace: FONT, fontSize: 14, bold: true, color: BLUE, charSpacing: 2, isTextBox: true, margin: 0,
  });
  s.addText(
    [
      { text: "How Secure World ", options: { color: NAVY } },
      { text: "Communicates", options: { color: BLUE } },
      { text: " With Linux", options: { color: NAVY } },
    ],
    { x: 0.7, y: 2.5, w: 11.8, h: 1.1, fontFace: FONT, fontSize: 36, bold: true, isTextBox: true, margin: 0, lineSpacing: 42 }
  );
  s.addShape("line", { x: 0.72, y: 3.75, w: 1.0, h: 0, line: { color: BLUE, width: 3 } });
  s.addText("Challenge-Response Heartbeat for Linux Liveness Detection", {
    x: 0.7, y: 3.9, w: 10.8, h: 0.5, fontFace: FONT, fontSize: 16, color: SUBTLE, isTextBox: true, margin: 0,
  });
  s.addText("A new, additive capability — does not change or depend on the existing CAN tamper/omission detection", {
    x: 0.7, y: 6.6, w: 10.5, h: 0.4, fontFace: FONT, fontSize: 12, italic: true, color: GREY, isTextBox: true, margin: 0,
  });
  addLogo(s, { x: 11.9, y: 0.5, w: 0.9, h: 0.9 });
}

// ============================================================ SLIDE 2 — THE COMMUNICATION MECHANISM
{
  const s = p.addSlide();
  addHeader(s, "The Underlying Mechanism", "Secure World ↔ Linux Share Memory, Not a Wire Protocol", "");
  card(s, { x: 0.5, y: 1.9, w: 11.8, h: 1.3, fill: GREEN_BG, border: GREEN });
  s.addText(
    "Core2 and Linux are different CPU cores on the SAME chip. “Communication” between them is not a transmission — it's one side writing ordinary DRAM, and the other side reading it, with a hardware doorbell in between so the reader knows when to look.",
    { x: 0.8, y: 2.1, w: 11.2, h: 0.9, fontFace: FONT, fontSize: 14, color: INK, isTextBox: true, margin: 0, lineSpacingMultiple: 1.3 }
  );
  card(s, { x: 0.5, y: 3.5, w: 5.75, h: 3.2, fill: "FFFFFF" });
  s.addText("THE SHARED MEMORY", { x: 0.75, y: 3.65, w: 5, h: 0.3, fontFace: FONT, fontSize: 12, bold: true, color: NAVY });
  bulletBox(s, [
    "Two small structs in ordinary DRAM",
    "heartbeat_challenge — Secure World writes, Linux reads",
    "heartbeat_response — Linux writes, Secure World reads",
    "Each side only ever writes its own struct",
  ], { x: 0.75, y: 4.0, w: 5.3, h: 2.5, fontSize: 12.5 });

  card(s, { x: 6.5, y: 3.5, w: 6.3, h: 3.2, fill: "FFFFFF" });
  s.addText("THE DOORBELL", { x: 6.75, y: 3.65, w: 5, h: 0.3, fontFace: FONT, fontSize: 12, bold: true, color: NAVY });
  bulletBox(s, [
    "A real hardware mailbox register, built into the BCM2837 SoC",
    "One dedicated slot per core — ringing Core0's slot only wakes Core0",
    "Same exact mechanism already used for CAN frame delivery (Core1→Linux)",
    "No new hardware, no new wiring — reused as-is",
  ], { x: 6.75, y: 4.0, w: 5.8, h: 2.5, fontSize: 12.5 });
  addFooter(s, 2, TOTAL);
}

// ============================================================ SLIDE 3 — WHY THIS IS NEEDED
{
  const s = p.addSlide();
  addHeader(s, "Why Add This", "A Gap the Original Design Never Claimed to Close", "");
  card(s, { x: 0.5, y: 1.9, w: 11.8, h: 1.9, fill: ORANGE_BG, border: ORANGE });
  s.addText(
    "The Core1+Core2 split guarantees CAN-frame tamper/omission detection independent of Linux — but it never checked whether Linux itself is alive. A fully hung or crashed Linux kernel was previously invisible to the system.",
    { x: 0.8, y: 2.1, w: 11.2, h: 1.6, fontFace: FONT, fontSize: 15, color: INK, isTextBox: true, margin: 0, lineSpacingMultiple: 1.35 }
  );
  bulletBox(s, [
    "This heartbeat adds: can we also tell when Linux itself has hung or crashed?",
    "This is a NEW capability, not a fix to a hole in the existing frame-integrity guarantee",
    "The existing guarantee never depended on Linux's liveness in the first place",
  ], { x: 0.8, y: 4.1, w: 11.2, h: 2.0, fontSize: 14, color: NAVY });
  addFooter(s, 3, TOTAL);
}

// ============================================================ SLIDE 4 — FLOW DIAGRAM
{
  const s = p.addSlide();
  addHeader(s, "Flow Diagram", "Challenge → Response → Verdict", "");

  box(s, "CORE 2 (Secure World)\nreads CNTPCT\n(own timer)", { x: 0.5, y: 1.85, w: 3.0, h: 1.1, fill: BLUE, border: NAVY });
  arrow(s, { x: 3.5, y: 2.4, w: 1.0, label: "write nonce" });
  box(s, "heartbeat_challenge\n{nonce, seq}\n(shared DRAM)", { x: 4.5, y: 1.85, w: 2.6, h: 1.1, fill: GREY_BG, border: GREY, color: INK });
  arrow(s, { x: 7.1, y: 2.4, w: 1.0, label: "doorbell" });
  box(s, "CORE 0 (Linux)\nCAN-consumer thread", { x: 8.1, y: 1.85, w: 3.2, h: 1.1, fill: GREEN_BG, border: GREEN, color: INK });

  arrow(s, { x: 9.7, y: 2.95, h: 0.8, label: "" });
  box(s, "writes nonce_echo back\ninto heartbeat_response", { x: 8.1, y: 3.75, w: 3.2, h: 0.9, fill: "FFFFFF", border: CARD_BORDER, color: INK, fontSize: 11 });
  arrow(s, { x: 8.1, y: 4.2, w: -4.0, label: "doorbell back" });

  box(s, "CORE 2 checks:\nseq match? nonce match?\nwithin deadline?", { x: 0.5, y: 3.75, w: 3.0, h: 1.3, fill: BLUE, border: NAVY, fontSize: 11 });

  // verdict branches
  card(s, { x: 0.5, y: 5.4, w: 5.6, h: 1.4, fill: GREEN_BG, border: GREEN });
  s.addText("PASS", { x: 0.7, y: 5.5, w: 2, h: 0.3, fontFace: FONT, fontSize: 12, bold: true, color: GREEN });
  s.addText("consecutive_misses = 0 — record latency, continue normally", { x: 0.7, y: 5.8, w: 5.2, h: 0.8, fontFace: FONT, fontSize: 12, color: INK });

  card(s, { x: 6.5, y: 5.4, w: 5.8, h: 1.4, fill: RED_BG, border: RED });
  s.addText("MISS (×3 consecutive)", { x: 6.7, y: 5.5, w: 4, h: 0.3, fontFace: FONT, fontSize: 12, bold: true, color: RED });
  s.addText("flag Linux unresponsive — log a tamper-log entry — Core1/Core2 CAN pipeline unaffected", { x: 6.7, y: 5.8, w: 5.4, h: 0.8, fontFace: FONT, fontSize: 12, color: INK });
  addFooter(s, 4, TOTAL);
}

// ============================================================ SLIDE 5 — RUNNING IN PARALLEL
{
  const s = p.addSlide();
  addHeader(s, "Independence", "Runs Beside the CAN Pipeline — Never Gates It", "");
  card(s, { x: 0.5, y: 1.9, w: 5.75, h: 3.9, fill: "FFFFFF" });
  s.addText("HEARTBEAT CHECK", { x: 0.75, y: 2.05, w: 5, h: 0.3, fontFace: FONT, fontSize: 13, bold: true, color: BLUE });
  bulletBox(s, [
    "Runs every Core2 loop iteration, unconditionally",
    "Checked BEFORE any frame-validation work — never starved by a traffic burst",
    "Independent ARM timer (CNTPCT), never Linux's clock",
  ], { x: 0.75, y: 2.45, w: 5.3, h: 3.2, fontSize: 13 });

  card(s, { x: 6.5, y: 1.9, w: 6.3, h: 3.9, fill: "FFFFFF" });
  s.addText("CAN INTEGRITY PIPELINE", { x: 6.75, y: 2.05, w: 5.5, h: 0.3, fontFace: FONT, fontSize: 13, bold: true, color: BLUE });
  bulletBox(s, [
    "Core1 captures, Core2 validates HMAC + counter — same as before",
    "Never checks or waits on the heartbeat's verdict",
    "Keeps working correctly even while Linux is flagged unresponsive",
  ], { x: 6.75, y: 2.45, w: 5.8, h: 3.2, fontSize: 13 });

  card(s, { x: 0.5, y: 6.0, w: 11.8, h: 0.85, fill: GREEN_BG, border: GREEN });
  s.addText("Key property: a Linux that answers heartbeats perfectly while tampering with frames is STILL caught — the two layers cannot disguise each other's failures.", {
    x: 0.75, y: 6.12, w: 11.3, h: 0.6, fontFace: FONT, fontSize: 13, italic: true, color: NAVY, valign: "middle",
  });
  addFooter(s, 5, TOTAL);
}

// ============================================================ SLIDE 6 — WHAT IT PROVES / DOES NOT PROVE
{
  const s = p.addSlide();
  addHeader(s, "Honest Scope", "What This Heartbeat Proves — and What It Doesn't", "");
  card(s, { x: 0.5, y: 1.9, w: 5.75, h: 4.3, fill: GREEN_BG, border: GREEN });
  s.addText("PROVES", { x: 0.75, y: 2.05, w: 5, h: 0.3, fontFace: FONT, fontSize: 13, bold: true, color: GREEN });
  bulletBox(s, [
    "The specific CAN-consumer thread is scheduled and responsive right now",
    "Within a deadline derived from real, measured round-trip latency",
    "Catches full kernel hangs, crashed/killed handler paths, broken interrupt routing",
  ], { x: 0.75, y: 2.5, w: 5.3, h: 3.5, fontSize: 13 });

  card(s, { x: 6.5, y: 1.9, w: 6.3, h: 4.3, fill: RED_BG, border: RED });
  s.addText("DOES NOT PROVE", { x: 6.75, y: 2.05, w: 5.5, h: 0.3, fontFace: FONT, fontSize: 13, bold: true, color: RED });
  bulletBox(s, [
    "That any specific CAN frame was processed correctly (already guaranteed independently by Core1+Core2)",
    "That Linux isn't lying about something else — caught separately by HMAC/counter checks",
    "A diagnosis of WHY a response was late — only that it was late",
  ], { x: 6.75, y: 2.5, w: 5.8, h: 3.5, fontSize: 13 });
  addFooter(s, 6, TOTAL);
}

// ============================================================ SLIDE 7 — DESIGN SAFEGUARDS
{
  const s = p.addSlide();
  addHeader(s, "Design Safeguards", "Why This Version Avoids False Positives", "");
  const rows = [
    ["Debounced, not single-miss", "Requires 3 consecutive missed/invalid responses before declaring failure"],
    ["Empirically derived deadline", "Measured round-trip latency under real load, not a guessed number"],
    ["Tied to the real consumer thread", "Response written by the same thread draining Core1's ring buffer — not a generic watchdog"],
    ["Latency trend, not just pass/fail", "Rolling history reveals gradual degradation, not just sharp failure"],
  ];
  let y = 2.0;
  rows.forEach(([a, b], i) => {
    card(s, { x: 0.5, y, w: 11.8, h: 0.95, fill: i % 2 === 0 ? "FFFFFF" : BG_LIGHT });
    s.addText(a, { x: 0.75, y: y + 0.08, w: 4.3, h: 0.8, fontFace: FONT, fontSize: 13, bold: true, color: NAVY, valign: "middle" });
    s.addText(b, { x: 5.2, y: y + 0.08, w: 6.9, h: 0.8, fontFace: FONT, fontSize: 12.5, color: SUBTLE, valign: "middle" });
    y += 1.05;
  });
  addFooter(s, 7, TOTAL);
}

// ============================================================ SLIDE 8 — ASK
{
  const s = p.addSlide();
  s.background = { color: NAVY };
  s.addText("WHAT WE'RE ASKING THE PROFESSOR TO CONFIRM", { x: 0.7, y: 1.5, w: 11, h: 0.4, fontFace: FONT, fontSize: 15, bold: true, color: BLUE_LT, charSpacing: 2 });
  bulletBox(s, [
    "Is Linux-liveness detection in scope, as an addition alongside CAN-frame tamper/omission detection?",
    "Does the debounced, empirically-derived heartbeat design meet expectations for false-positive handling?",
    "Should this run entirely within Core2's existing loop, or warrant its own dedicated secure core?",
  ], { x: 0.7, y: 2.3, w: 11.3, h: 2.8, fontSize: 16, color: "FFFFFF", lineSpacingMultiple: 1.5 });
  s.addText("This is an additive capability — it does not change or depend on the already-verified CAN tamper/omission detection design.", {
    x: 0.7, y: 5.6, w: 11.3, h: 0.8, fontFace: FONT, fontSize: 13, italic: true, color: GREY, isTextBox: true, margin: 0, lineSpacingMultiple: 1.3,
  });
  addFooter(s, 8, TOTAL, true);
}

p.writeFile({ fileName: "/home/aslam/Minor_Project/Secure_CAN_Heartbeat_Design.pptx" }).then(() => {
  console.log("Deck written: Secure_CAN_Heartbeat_Design.pptx");
});
