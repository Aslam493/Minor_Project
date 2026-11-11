const pptxgen = require("pptxgenjs");

const LOGO = "/home/aslam/Minor_Project/midterm_deck_v2/html/IIT_Delhi_logo.png";

// ---- palette (same as build_deck.js) ----
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
  p.layout = "LAYOUT_WIDE"; // 13.3 x 7.5 in
  return p;
}

function addLogo(slide, opts = {}) {
  slide.addImage({ path: LOGO, x: opts.x ?? 12.55, y: opts.y ?? 0.28, w: 0.55, h: 0.55 });
}

function addFooter(slide, pageNum, total, dark) {
  const color = dark ? "6D7D96" : "8A8F98";
  slide.addText("Secure CAN Design Review — Core1/Core2 Split Proposal", {
    x: 0.5, y: 7.12, w: 7, h: 0.3, fontFace: FONT, fontSize: 10, color, isTextBox: true, margin: 0,
  });
  slide.addText(`${pageNum} / ${total}`, {
    x: 12.0, y: 7.12, w: 0.9, h: 0.3, fontFace: FONT, fontSize: 10, color, align: "right", isTextBox: true, margin: 0,
  });
}

function addHeader(slide, kicker, title, subtitle, kickerColor = BLUE) {
  slide.addText(kicker.toUpperCase(), {
    x: 0.5, y: 0.32, w: 10, h: 0.3, fontFace: FONT, fontSize: 13, bold: true, color: kickerColor,
    charSpacing: 2, isTextBox: true, margin: 0,
  });
  slide.addShape("line", { x: 0.5, y: 0.62, w: 11.4, h: 0, line: { color: NAVY, width: 1.2 } });
  slide.addText(title, {
    x: 0.5, y: 0.74, w: 11.8, h: 0.62, fontFace: FONT, fontSize: 28, bold: true, color: NAVY, isTextBox: true, margin: 0,
  });
  if (subtitle) {
    slide.addText(subtitle, {
      x: 0.5, y: 1.3, w: 11.8, h: 0.4, fontFace: FONT, fontSize: 13, color: SUBTLE, isTextBox: true, margin: 0,
    });
  }
}

function bulletBox(slide, items, opts) {
  const { x, y, w, h, fontSize = 13, color = INK, bullet = true, lineSpacingMultiple = 1.25 } = opts;
  slide.addText(
    items.map((t) => ({ text: t, options: { breakLine: true } })),
    {
      x, y, w, h, fontFace: FONT, fontSize, color, isTextBox: true,
      bullet: bullet ? { code: "2022", indent: 18 } : false,
      lineSpacingMultiple, valign: "top", margin: 0,
    }
  );
}

function card(slide, { x, y, w, h, fill = "FFFFFF", border = CARD_BORDER, radius = 0.06 }) {
  slide.addShape("roundRect", {
    x, y, w, h, rectRadius: radius,
    fill: { color: fill }, line: { color: border, width: 1 },
  });
}

const TOTAL_SLIDES = 12;
const p = pres();

// ============================================================= SLIDE 1 — TITLE
{
  const s = p.addSlide();
  s.background = { color: "F2F3F5" };
  s.addText("MINOR PROJECT  |  DESIGN REVIEW FOR APPROVAL", {
    x: 0.7, y: 1.7, w: 10, h: 0.35, fontFace: FONT, fontSize: 14, bold: true, color: BLUE,
    charSpacing: 2, isTextBox: true, margin: 0,
  });
  s.addText(
    [
      { text: "Secure CAN Gateway — ", options: { color: NAVY } },
      { text: "Core1 + Core2", options: { color: BLUE } },
      { text: " Split Architecture", options: { color: NAVY } },
    ],
    { x: 0.7, y: 2.3, w: 11.8, h: 1.3, fontFace: FONT, fontSize: 36, bold: true, isTextBox: true, margin: 0, lineSpacing: 42 }
  );
  s.addShape("line", { x: 0.72, y: 3.65, w: 1.0, h: 0, line: { color: BLUE, width: 3 } });
  s.addText(
    "Responding directly to the detection-not-prevention requirement — what changes, why, and what stays the same",
    { x: 0.7, y: 3.8, w: 10.8, h: 0.5, fontFace: FONT, fontSize: 16, color: SUBTLE, isTextBox: true, margin: 0 }
  );
  s.addText("Prepared for review before implementation begins", {
    x: 0.7, y: 6.6, w: 8, h: 0.35, fontFace: FONT, fontSize: 12, italic: true, color: GREY, isTextBox: true, margin: 0,
  });
  addLogo(s, { x: 11.9, y: 0.5, w: 0.9, h: 0.9 });
}

// ============================================================= SLIDE 2 — WHAT WE HAD + WHY REJECTED
{
  const s = p.addSlide();
  addHeader(s, "Starting Point", "What the Design Had, and Why It Was Rejected", "");
  card(s, { x: 0.5, y: 1.85, w: 5.75, h: 4.9, fill: GREY_BG });
  s.addText("BEFORE", { x: 0.75, y: 2.0, w: 5, h: 0.35, fontFace: FONT, fontSize: 13, bold: true, color: GREY });
  bulletBox(s, [
    "Secure World (Core1) owned the CAN controller directly",
    "GPIO25 interrupt routed to Secure World as an FIQ",
    "Core1 validated every frame (HMAC-SHA256 + counter check) before Linux ever saw it",
    "Only after full validation did Linux receive the frame",
  ], { x: 0.75, y: 2.45, w: 5.3, h: 3.0, fontSize: 14 });

  card(s, { x: 6.5, y: 1.85, w: 6.3, h: 4.9, fill: RED_BG, border: RED });
  s.addText("PROFESSOR'S REJECTION", { x: 6.75, y: 2.0, w: 5, h: 0.35, fontFace: FONT, fontSize: 13, bold: true, color: RED });
  s.addText(
    `"packet of controller goes into normal world normally, secure world monitors every packet... if packet is corrupted, make a log of it. I have not prevented the attacker from touching the packet — if they do touch it, I have a secure log in flash."`,
    { x: 6.75, y: 2.5, w: 5.8, h: 2.0, fontFace: FONT, fontSize: 13, italic: true, color: INK, isTextBox: true, margin: 0, lineSpacingMultiple: 1.3 }
  );
  s.addText("Core requirement extracted:", { x: 6.75, y: 4.65, w: 5, h: 0.3, fontFace: FONT, fontSize: 13, bold: true, color: NAVY });
  bulletBox(s, [
    "Detection, not prevention",
    "Linux must get the frame fast",
    "Secure World stays Linux-untouchable",
  ], { x: 6.75, y: 5.0, w: 5.8, h: 1.5, fontSize: 13 });
  addFooter(s, 2, TOTAL_SLIDES);
}

// ============================================================= SLIDE 3 — DESIGNS TRIED AND REJECTED
{
  const s = p.addSlide();
  addHeader(s, "Design Evolution", "Every Alternative Tried, and Why Each Failed", "");
  const rows = [
    ["Linux-first + shared-memory snoop", "Compromised Linux can skip writing to shared memory — omission gap stays open"],
    ["CS-line / GPIO watching", "Proves a transaction happened, not what data was in it"],
    ["Independent register peek on a timer", "SPI bus collision risk + unverified register semantics"],
    ["Mandatory kernel-driver push", "A fully compromised kernel can just skip the push call"],
    ["Dynamic SPI0/GPIO ownership handover", "Requires live TF-A/interrupt changes — same risk class as a prior board-hang incident"],
    ["can1 / external MCU as witness", "can1 has an unresolved hardware fault; unnecessary once the final design is found"],
  ];
  let y = 1.95;
  const rowH = 0.72;
  rows.forEach(([a, b], i) => {
    card(s, { x: 0.5, y, w: 11.8, h: rowH - 0.1, fill: i % 2 === 0 ? "FFFFFF" : BG_LIGHT, border: CARD_BORDER });
    s.addText(a, { x: 0.75, y: y + 0.08, w: 4.2, h: rowH - 0.25, fontFace: FONT, fontSize: 13, bold: true, color: NAVY, valign: "middle" });
    s.addText(b, { x: 5.1, y: y + 0.08, w: 7.0, h: rowH - 0.25, fontFace: FONT, fontSize: 12.5, color: SUBTLE, valign: "middle" });
    y += rowH;
  });
  addFooter(s, 3, TOTAL_SLIDES);
}

// ============================================================= SLIDE 4 — THE WINNING DESIGN
{
  const s = p.addSlide();
  addHeader(s, "The Final Design", "Core1 (Capture/Forward) + Core2 (Validate/Log)", "");
  card(s, { x: 0.5, y: 1.85, w: 11.8, h: 1.5, fill: GREEN_BG, border: GREEN });
  s.addText(
    "Hardware ownership never moves to Linux. Core1 stays exactly where it already is today — it just forwards fast and lets a second dedicated core do the crypto off to the side.",
    { x: 0.75, y: 2.05, w: 11.3, h: 1.1, fontFace: FONT, fontSize: 15, color: INK, isTextBox: true, margin: 0, lineSpacingMultiple: 1.3 }
  );

  card(s, { x: 0.5, y: 3.6, w: 5.75, h: 3.1, fill: "FFFFFF" });
  s.addText("CORE 1 — Fast-Path Forwarder", { x: 0.75, y: 3.75, w: 5.2, h: 0.35, fontFace: FONT, fontSize: 14, bold: true, color: BLUE });
  bulletBox(s, [
    "Owns SPI0/GPIO25 — unchanged from today",
    "Captures the frame off the wire",
    "Increments its own ground-truth counter",
    "Forwards immediately to Linux — zero crypto",
    "Separately hands a copy to Core2",
  ], { x: 0.75, y: 4.15, w: 5.3, h: 2.4, fontSize: 13 });

  card(s, { x: 6.5, y: 3.6, w: 6.3, h: 3.1, fill: "FFFFFF" });
  s.addText("CORE 2 — Validator + Logger", { x: 6.75, y: 3.75, w: 5.5, h: 0.35, fontFace: FONT, fontSize: 14, bold: true, color: BLUE });
  bulletBox(s, [
    "Newly diverted into Secure World (same TF-A mechanism as Core1)",
    "Recomputes HMAC-SHA256, compares to received tag",
    "Checks counter continuity (replay/reorder)",
    "On any failure: appends to a tamper-evident, hash-chained log",
  ], { x: 6.75, y: 4.15, w: 5.8, h: 2.4, fontSize: 13 });
  addFooter(s, 4, TOTAL_SLIDES);
}

// ============================================================= SLIDE 5 — WHY IT CLOSES THE GAP
{
  const s = p.addSlide();
  addHeader(s, "Why This One Works", "Closing the Omission Gap — Structurally, Not Best-Effort", "");
  s.addText(
    "Every earlier attempt tried to let Secure World independently verify a setup where Linux touches the hardware first. That ordering itself is what creates the omission gap.",
    { x: 0.5, y: 1.95, w: 11.8, h: 0.8, fontFace: FONT, fontSize: 15, color: INK, isTextBox: true, margin: 0, lineSpacingMultiple: 1.3 }
  );
  card(s, { x: 0.5, y: 2.95, w: 11.8, h: 2.9, fill: GREEN_BG, border: GREEN });
  bulletBox(s, [
    "Core1 is still the first and only thing that ever touches the wire — exactly like today",
    "There is no point at which a compromised Linux could hide a frame from Secure World",
    "Secure World's knowledge of what arrived never depends on Linux's cooperation",
    "can1, external hardware, and liveness timers all become unnecessary",
  ], { x: 0.8, y: 3.2, w: 11.1, h: 2.5, fontSize: 15, color: INK });
  addFooter(s, 5, TOTAL_SLIDES);
}

// ============================================================= SLIDE 6 — ARCHITECTURE DIAGRAM
{
  const s = p.addSlide();
  addHeader(s, "Architecture", "End-to-End Flow", "");

  // CAN bus -> chip -> core1
  card(s, { x: 0.5, y: 1.85, w: 2.6, h: 0.9, fill: GREY_BG });
  s.addText("CAN Bus\n(wire)", { x: 0.5, y: 1.95, w: 2.6, h: 0.7, align: "center", fontFace: FONT, fontSize: 12, color: INK, valign: "middle" });

  s.addShape("line", { x: 3.1, y: 2.3, w: 0.7, h: 0, line: { color: GREY, width: 2, endArrowType: "triangle" } });

  card(s, { x: 3.8, y: 1.85, w: 2.6, h: 0.9, fill: GREY_BG });
  s.addText("MCP2518FD\nController", { x: 3.8, y: 1.95, w: 2.6, h: 0.7, align: "center", fontFace: FONT, fontSize: 12, color: INK, valign: "middle" });

  s.addShape("line", { x: 6.4, y: 2.3, w: 0.7, h: 0, line: { color: GREY, width: 2, endArrowType: "triangle" } });

  card(s, { x: 7.1, y: 1.6, w: 3.0, h: 1.4, fill: BLUE, border: NAVY });
  s.addText("CORE 1\n(Secure World)\ncapture + forward", { x: 7.1, y: 1.72, w: 3.0, h: 1.15, align: "center", fontFace: FONT, fontSize: 12, bold: true, color: "FFFFFF", valign: "middle" });

  // branch down
  s.addShape("line", { x: 8.0, y: 3.0, w: 0, h: 0.7, line: { color: NAVY, width: 2, endArrowType: "triangle" } });
  s.addShape("line", { x: 9.3, y: 3.0, w: 0, h: 0.7, line: { color: NAVY, width: 2, endArrowType: "triangle" } });

  card(s, { x: 6.0, y: 3.8, w: 3.0, h: 1.2, fill: GREEN_BG, border: GREEN });
  s.addText("CORE 0 (Linux)\nFast path\nno crypto wait", { x: 6.0, y: 3.9, w: 3.0, h: 1.0, align: "center", fontFace: FONT, fontSize: 12, color: INK, valign: "middle" });

  card(s, { x: 9.3, y: 3.8, w: 3.0, h: 1.2, fill: ORANGE_BG, border: ORANGE });
  s.addText("CORE 2 (Secure)\nHMAC + counter check\ntamper log on fail", { x: 9.3, y: 3.9, w: 3.0, h: 1.0, align: "center", fontFace: FONT, fontSize: 12, color: INK, valign: "middle" });

  card(s, { x: 0.5, y: 5.4, w: 11.8, h: 1.3, fill: "FFFFFF", border: CARD_BORDER });
  s.addText(
    "Key point: both branches start from the SAME first-hand capture by Core1. Linux never gets a chance to see the frame before Secure World already has it.",
    { x: 0.8, y: 5.6, w: 11.2, h: 0.9, fontFace: FONT, fontSize: 13, italic: true, color: NAVY, valign: "middle" }
  );
  addFooter(s, 6, TOTAL_SLIDES);
}

// ============================================================= SLIDE 7 — LATENCY FIX
{
  const s = p.addSlide();
  addHeader(s, "Addressing the Latency Objection", "SPI-Level Optimizations on Top of the Split", "");
  const rows = [
    ["9 SPI transactions per frame", "Collapsed to 3–4 (FIFOUA+INT, message object, FIFOCON UINC, INT clear)"],
    ["SPI clock at ~7.8 MHz (divider 32)", "Raised to ~15.6–20.8 MHz (divider 12–16)"],
    ["Chip housekeeping done before notifying Linux", "Linux notified immediately; housekeeping moves after"],
    ["Single-slot buffer — drops under burst", "64-slot lock-free ring buffer — never blocks on a consumer"],
  ];
  let y = 2.0;
  rows.forEach(([a, b], i) => {
    card(s, { x: 0.5, y, w: 11.8, h: 1.0, fill: i % 2 === 0 ? "FFFFFF" : BG_LIGHT });
    s.addText("BEFORE", { x: 0.75, y: y + 0.08, w: 1.2, h: 0.3, fontFace: FONT, fontSize: 10, bold: true, color: RED });
    s.addText(a, { x: 0.75, y: y + 0.35, w: 5.3, h: 0.55, fontFace: FONT, fontSize: 12.5, color: INK, valign: "top" });
    s.addText("AFTER", { x: 6.3, y: y + 0.08, w: 1.2, h: 0.3, fontFace: FONT, fontSize: 10, bold: true, color: GREEN });
    s.addText(b, { x: 6.3, y: y + 0.35, w: 5.8, h: 0.55, fontFace: FONT, fontSize: 12.5, color: INK, valign: "top" });
    y += 1.1;
  });
  addFooter(s, 7, TOTAL_SLIDES);
}

// ============================================================= SLIDE 8 — ACCEPTED TRADEOFF
{
  const s = p.addSlide();
  addHeader(s, "The One Accepted Tradeoff", "Detection Window — Stated Explicitly, Not Hidden", "");
  card(s, { x: 1.5, y: 2.3, w: 9.8, h: 2.6, fill: ORANGE_BG, border: ORANGE });
  s.addText(
    "Linux acts on a frame a short, measured time BEFORE Core2 finishes validating it.",
    { x: 1.9, y: 2.55, w: 9.0, h: 0.6, fontFace: FONT, fontSize: 18, bold: true, color: NAVY, align: "center" }
  );
  s.addText(
    "This is the deliberate, accepted tradeoff under “detection, not prevention” — not an oversight.\nTarget to be confirmed with real ARM PMCCNTR cycle-counter measurement on hardware.",
    { x: 1.9, y: 3.3, w: 9.0, h: 1.3, fontFace: FONT, fontSize: 14, color: INK, align: "center", lineSpacingMultiple: 1.4 }
  );
  addFooter(s, 8, TOTAL_SLIDES);
}

// ============================================================= SLIDE 9 — HEARTBEAT ADDITION (one slide only)
{
  const s = p.addSlide();
  addHeader(s, "Additional Design — Under Review", "Heartbeat Liveness Check (Linux Hang/Crash Detection)", "");

  card(s, { x: 0.5, y: 1.85, w: 11.8, h: 1.0, fill: ORANGE_BG, border: ORANGE });
  s.addText(
    "New capability, not a fix: the Core1+Core2 split never needed Linux's liveness to guarantee CAN-frame integrity. This adds detection for a fully hung/crashed Linux kernel, on top of that already-complete guarantee.",
    { x: 0.75, y: 1.95, w: 11.3, h: 0.8, fontFace: FONT, fontSize: 12.5, color: INK, isTextBox: true, margin: 0, lineSpacingMultiple: 1.25 }
  );

  s.addText("Mechanism (runs inside Core2's existing loop — no new core):", { x: 0.5, y: 3.0, w: 8, h: 0.3, fontFace: FONT, fontSize: 12, bold: true, color: NAVY });
  bulletBox(s, [
    "Core2 sends a fresh random nonce to Linux's CAN-consumer thread (same shared-memory + mailbox-doorbell mechanism already used for CAN frames)",
    "Linux's CAN-consumer thread echoes the nonce back before a deadline derived from measured round-trip latency",
    "3 consecutive missed/invalid responses → “Linux unresponsive” logged; a single late beat is not enough (debounced)",
    "Core1's capture and Core2's HMAC/counter validation continue unaffected — this check never gates them",
  ], { x: 0.5, y: 3.35, w: 11.4, h: 2.3, fontSize: 12.5 });

  card(s, { x: 0.5, y: 5.75, w: 11.8, h: 0.95, fill: GREEN_BG, border: GREEN });
  s.addText(
    "Key property: a Linux that answers heartbeats perfectly while tampering with frames is still caught — the two layers cannot disguise each other's failures.",
    { x: 0.75, y: 5.87, w: 11.3, h: 0.7, fontFace: FONT, fontSize: 12.5, italic: true, color: NAVY, valign: "middle" }
  );
  addFooter(s, 9, TOTAL_SLIDES);
}

// ============================================================= SLIDE 10 — BUILD PLAN
{
  const s = p.addSlide();
  addHeader(s, "Implementation Plan", "What Will Actually Be Built", "");
  const steps = [
    ["1", "Divert Core2 into Secure World", "Replicate the proven Core1 TF-A/PSCI mechanism for Core2's position"],
    ["2", "Collapse SPI transactions (9→3–4)", "With explicit BDIAG1/IVMIF/CERRIF diagnostic fallback — zero loss of error visibility"],
    ["3", "Wire up Core1→Linux fast path", "64-slot SPSC ring buffer in shared DRAM + mailbox doorbell"],
    ["4", "Core1→Core2 cross-core queue", "New, separate Secure-World-only channel — Linux cannot see it"],
    ["5", "Core2 validation + tamper log", "Reuse existing HMAC/counter logic; hash-chained log entries"],
  ];
  let y = 1.95;
  steps.forEach(([n, title, desc]) => {
    card(s, { x: 0.5, y, w: 0.6, h: 0.75, fill: BLUE });
    s.addText(n, { x: 0.5, y, w: 0.6, h: 0.75, align: "center", valign: "middle", fontFace: FONT, fontSize: 18, bold: true, color: "FFFFFF" });
    s.addText(title, { x: 1.3, y: y + 0.02, w: 10.9, h: 0.35, fontFace: FONT, fontSize: 14, bold: true, color: NAVY });
    s.addText(desc, { x: 1.3, y: y + 0.36, w: 10.9, h: 0.35, fontFace: FONT, fontSize: 12, color: SUBTLE });
    y += 0.92;
  });
  addFooter(s, 10, TOTAL_SLIDES);
}

// ============================================================= SLIDE 11 — SAFETY + VERIFICATION
{
  const s = p.addSlide();
  addHeader(s, "Before Touching Real Hardware", "Safety Checkpoint and Verification Plan", "");
  card(s, { x: 0.5, y: 1.9, w: 5.75, h: 4.7, fill: RED_BG, border: RED });
  s.addText("SAFETY REQUIREMENT", { x: 0.75, y: 2.05, w: 5, h: 0.3, fontFace: FONT, fontSize: 13, bold: true, color: RED });
  bulletBox(s, [
    "Core2's diversion touches TF-A warm-boot/PSCI code — same class that caused a prior board-hang incident",
    "UART must be connected before any flash of this change",
    "Backup checkpoint taken first: SD card image + KNOWLEDGE.md entry, same convention as prior checkpoints",
  ], { x: 0.75, y: 2.45, w: 5.3, h: 4.0, fontSize: 13 });

  card(s, { x: 6.5, y: 1.9, w: 6.3, h: 4.7, fill: "FFFFFF" });
  s.addText("VERIFICATION STEPS", { x: 6.75, y: 2.05, w: 5, h: 0.3, fontFace: FONT, fontSize: 13, bold: true, color: NAVY });
  bulletBox(s, [
    "Core2 isolation: heartbeat only, confirm it never reaches Linux",
    "Core1→Core2 handoff correctness via debug PTA readback",
    "HMAC/counter parity vs. Core1's already-proven results",
    "Tamper log: inject bad frame, confirm log + chain-break detection",
    "Real PMCCNTR latency measurement, not assumed numbers",
    "Burst test at 1000+ fps, confirm zero drops",
  ], { x: 6.75, y: 2.45, w: 5.9, h: 4.0, fontSize: 12.5 });
  addFooter(s, 11, TOTAL_SLIDES);
}

// ============================================================= SLIDE 12 — ASK
{
  const s = p.addSlide();
  s.background = { color: NAVY };
  s.addText("WHAT WE'RE ASKING THE PROFESSOR TO CONFIRM", {
    x: 0.7, y: 1.3, w: 11, h: 0.4, fontFace: FONT, fontSize: 15, bold: true, color: BLUE_LT, charSpacing: 2,
  });
  bulletBox(s, [
    "This Core1+Core2 split as the approach — hardware ownership unchanged, validation moved off the critical path",
    "The explicit, measured detection window as an accepted tradeoff (not a gap)",
    "Log reboot-persistence shipped as RAM-only for this milestone; persistent storage is future work",
    "can1's hardware fault remains open but is no longer blocking this design's security goal",
  ], { x: 0.7, y: 2.0, w: 11.3, h: 3.0, fontSize: 16, color: "FFFFFF", lineSpacingMultiple: 1.5 });
  s.addText("On approval: implementation begins with a backup checkpoint, then proceeds step by step with UART-verified testing at each stage.", {
    x: 0.7, y: 5.6, w: 11.3, h: 0.8, fontFace: FONT, fontSize: 13, italic: true, color: GREY, isTextBox: true, margin: 0, lineSpacingMultiple: 1.3,
  });
  addFooter(s, 12, TOTAL_SLIDES, true);
}

p.writeFile({ fileName: "/home/aslam/Minor_Project/Secure_CAN_Design_Review.pptx" }).then(() => {
  console.log("Deck written: Secure_CAN_Design_Review.pptx");
});
