const pptxgen = require("pptxgenjs");

const LOGO = "/home/aslam/Minor_Project/midterm_deck_v2/html/IIT_Delhi_logo.png";

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
const FONT = "Arial";

function pres() {
  const p = new pptxgen();
  p.layout = "LAYOUT_WIDE";
  return p;
}
function addLogo(slide) {
  slide.addImage({ path: LOGO, x: 12.55, y: 0.28, w: 0.55, h: 0.55 });
}
function addFooter(slide, n, total) {
  slide.addText("Secure CAN Gateway — Design Flow Diagrams", {
    x: 0.5, y: 7.15, w: 7, h: 0.28, fontFace: FONT, fontSize: 9, color: GREY, isTextBox: true, margin: 0,
  });
  slide.addText(`${n} / ${total}`, { x: 12.0, y: 7.15, w: 0.9, h: 0.28, fontFace: FONT, fontSize: 9, color: GREY, align: "right", isTextBox: true, margin: 0 });
}
function title(slide, n, name) {
  slide.addText(`DESIGN ${n}`, { x: 0.5, y: 0.28, w: 4, h: 0.3, fontFace: FONT, fontSize: 12, bold: true, color: BLUE, charSpacing: 2, isTextBox: true, margin: 0 });
  slide.addText(name, { x: 0.5, y: 0.58, w: 11.8, h: 0.55, fontFace: FONT, fontSize: 24, bold: true, color: NAVY, isTextBox: true, margin: 0 });
  slide.addShape("line", { x: 0.5, y: 1.18, w: 11.4, h: 0, line: { color: CARD_BORDER, width: 1 } });
}
function box(slide, text, { x, y, w, h, fill, border, color = "FFFFFF", fontSize = 12, bold = true, dashed = false }) {
  slide.addShape("roundRect", {
    x, y, w, h, rectRadius: 0.08,
    fill: { color: fill }, line: { color: border || fill, width: dashed ? 1.5 : 1, dashType: dashed ? "dash" : "solid" },
  });
  slide.addText(text, { x, y, w, h, align: "center", valign: "middle", fontFace: FONT, fontSize, bold, color, isTextBox: true, margin: 2 });
}
function hArrow(slide, x, y, w, { color = NAVY, dashed = false, label } = {}) {
  slide.addShape("line", { x, y, w, h: 0, line: { color, width: 2, dashType: dashed ? "dash" : "solid", endArrowType: "triangle" } });
  if (label) slide.addText(label, { x, y: y - 0.3, w: Math.max(w, 1.4), h: 0.28, align: "center", fontFace: FONT, fontSize: 10, italic: true, color: SUBTLE });
}
function vArrow(slide, x, y, h, { color = NAVY, dashed = false, label } = {}) {
  slide.addShape("line", { x, y, w: 0, h, line: { color, width: 2, dashType: dashed ? "dash" : "solid", endArrowType: "triangle" } });
  if (label) slide.addText(label, { x: x + 0.1, y: y + h / 2 - 0.15, w: 2.2, h: 0.3, fontFace: FONT, fontSize: 10, italic: true, color: SUBTLE });
}
function caption(slide, text, { fill = GREY_BG, border = CARD_BORDER, color = INK } = {}) {
  slide.addShape("roundRect", { x: 0.5, y: 6.35, w: 11.8, h: 0.75, rectRadius: 0.07, fill: { color: fill }, line: { color: border, width: 1 } });
  slide.addText(text, { x: 0.75, y: 6.35, w: 11.3, h: 0.75, align: "center", valign: "middle", fontFace: FONT, fontSize: 14, italic: true, color, isTextBox: true, margin: 0 });
}
function xMark(slide, x, y) {
  slide.addShape("roundRect", { x, y, w: 0.4, h: 0.4, rectRadius: 0.2, fill: { color: RED }, line: { color: RED, width: 0 } });
  slide.addText("✕", { x, y, w: 0.4, h: 0.4, align: "center", valign: "middle", fontFace: FONT, fontSize: 14, bold: true, color: "FFFFFF", isTextBox: true, margin: 0 });
}

const TOTAL = 5;
const p = pres();

// ============================================================ DESIGN 1 — SECURE-FIRST (ORIGINAL)
{
  const s = p.addSlide();
  title(s, 1, "Secure-First (Original) — Too Slow");
  box(s, "CAN\nBus", { x: 0.5, y: 2.3, w: 1.8, h: 1.1, fill: GREY_BG, border: GREY, color: INK });
  hArrow(s, 2.5, 2.85, 1.0);
  box(s, "Controller", { x: 3.7, y: 2.3, w: 2.0, h: 1.1, fill: GREY_BG, border: GREY, color: INK });
  hArrow(s, 5.9, 2.85, 1.2);
  box(s, "Secure World\nFULL HMAC validate", { x: 7.3, y: 1.9, w: 3.2, h: 1.9, fill: BLUE, border: NAVY, fontSize: 13 });
  hArrow(s, 10.7, 2.85, 1.1);
  box(s, "Linux", { x: 11.9, y: 2.3, w: 0.9, h: 1.1, fill: GREEN_BG, border: GREEN, color: INK, fontSize: 11 });
  caption(s, "Every frame waits for full crypto before Linux sees it — rejected for latency", { fill: RED_BG, border: RED });
  addLogo(s); addFooter(s, 1, TOTAL);
}

// ============================================================ DESIGN 2 — LINUX-FIRST + SHARED MEMORY SNOOP
{
  const s = p.addSlide();
  title(s, 2, "Linux-First + Shared-Memory Snoop — Omission Gap");
  box(s, "CAN\nBus", { x: 0.5, y: 1.9, w: 1.6, h: 1.0, fill: GREY_BG, border: GREY, color: INK });
  hArrow(s, 2.3, 2.4, 0.9);
  box(s, "Controller", { x: 3.4, y: 1.9, w: 1.8, h: 1.0, fill: GREY_BG, border: GREY, color: INK });
  hArrow(s, 5.4, 2.4, 1.0);
  box(s, "Linux\n(owns hardware)", { x: 6.6, y: 1.6, w: 2.6, h: 1.6, fill: GREEN_BG, border: GREEN, color: INK, fontSize: 12 });
  vArrow(s, 7.9, 3.25, 0.9, { dashed: true, color: RED });
  xMark(s, 7.7, 4.0);
  box(s, "Shared Memory\n(maybe written)", { x: 6.6, y: 4.45, w: 2.6, h: 1.0, fill: "FFFFFF", border: RED, color: INK, fontSize: 11, dashed: true });
  hArrow(s, 9.4, 4.95, 1.0, { dashed: true });
  box(s, "Secure World\nreads a copy", { x: 10.6, y: 4.45, w: 2.2, h: 1.0, fill: BLUE, border: NAVY, fontSize: 11 });
  caption(s, "Compromised Linux can simply skip the write — Secure World never learns", { fill: RED_BG, border: RED });
  addLogo(s); addFooter(s, 2, TOTAL);
}

// ============================================================ DESIGN 7 — can1 / EXTERNAL WITNESS
{
  const s = p.addSlide();
  title(s, 3, "can1 / External Witness — Hardware Fault");
  box(s, "CAN Bus\n(can0)", { x: 0.5, y: 1.8, w: 2.0, h: 1.0, fill: GREY_BG, border: GREY, color: INK, fontSize: 12 });
  hArrow(s, 2.7, 2.3, 1.0);
  box(s, "Controller 0", { x: 3.9, y: 1.8, w: 2.0, h: 1.0, fill: GREY_BG, border: GREY, color: INK, fontSize: 12 });
  hArrow(s, 6.1, 2.3, 1.0);
  box(s, "Linux + Secure World\n(main pipeline, working)", { x: 7.3, y: 1.6, w: 4.4, h: 1.4, fill: GREEN_BG, border: GREEN, color: INK, fontSize: 12 });

  box(s, "CAN Bus\n(can1)", { x: 0.5, y: 4.0, w: 2.0, h: 1.0, fill: GREY_BG, border: GREY, color: INK, fontSize: 12 });
  hArrow(s, 2.7, 4.5, 1.0, { dashed: true, color: RED });
  box(s, "Controller 1\nosc = 0x0", { x: 3.9, y: 4.0, w: 2.0, h: 1.0, fill: RED_BG, border: RED, color: RED, fontSize: 12 });
  xMark(s, 6.0, 4.3);
  box(s, "Independent witness\n(never comes online)", { x: 6.4, y: 3.7, w: 3.0, h: 1.6, fill: RED_BG, border: RED, color: RED, fontSize: 12, dashed: true });
  caption(s, "Unresolved hardware fault — and unnecessary once Design 8 is found", { fill: RED_BG, border: RED });
  addLogo(s); addFooter(s, 3, TOTAL);
}

// ============================================================ DESIGN 8 — CORE1 + CORE2 (FINAL)
{
  const s = p.addSlide();
  title(s, 4, "Core1 + Core2 Split — the Final Design");
  box(s, "CAN\nBus", { x: 0.3, y: 2.6, w: 1.4, h: 1.0, fill: GREY_BG, border: GREY, color: INK, fontSize: 11 });
  hArrow(s, 1.7, 3.1, 0.8);
  box(s, "Controller", { x: 2.5, y: 2.6, w: 1.7, h: 1.0, fill: GREY_BG, border: GREY, color: INK, fontSize: 11 });
  hArrow(s, 4.2, 3.1, 0.9);
  box(s, "CORE 1\ncapture + forward", { x: 5.1, y: 2.3, w: 2.6, h: 1.6, fill: BLUE, border: NAVY, fontSize: 13 });
  vArrow(s, 6.0, 3.9, 0.9);
  vArrow(s, 7.0, 3.9, 0.9);
  box(s, "Linux\nfast path, zero crypto", { x: 4.2, y: 4.9, w: 3.2, h: 1.2, fill: GREEN_BG, border: GREEN, color: INK, fontSize: 12 });
  box(s, "CORE 2\nHMAC + counter + log", { x: 7.6, y: 4.9, w: 3.6, h: 1.2, fill: ORANGE_BG, border: ORANGE, color: INK, fontSize: 12 });
  caption(s, "Core1 is first to touch the wire, always — the omission gap is structurally closed", { fill: GREEN_BG, border: GREEN });
  addLogo(s); addFooter(s, 4, TOTAL);
}

// ============================================================ DESIGN 9 — HEARTBEAT ADDITION
{
  const s = p.addSlide();
  title(s, 5, "Heartbeat Liveness Check — Additive, Independent");
  box(s, "CORE 2\n(own timer)", { x: 0.5, y: 2.1, w: 2.4, h: 1.1, fill: BLUE, border: NAVY, fontSize: 12 });
  hArrow(s, 2.9, 2.65, 1.0, { label: "nonce" });
  box(s, "Linux CAN-\nconsumer thread", { x: 3.9, y: 2.1, w: 2.6, h: 1.1, fill: GREEN_BG, border: GREEN, color: INK, fontSize: 12 });
  hArrow(s, 6.5, 2.65, 1.0, { label: "echo back" });
  box(s, "CORE 2 verdict\nseq/nonce/deadline", { x: 7.5, y: 2.1, w: 2.7, h: 1.1, fill: BLUE, border: NAVY, fontSize: 12 });

  box(s, "PASS — continue", { x: 7.5, y: 3.7, w: 2.0, h: 0.8, fill: GREEN_BG, border: GREEN, color: INK, fontSize: 11 });
  box(s, "3 misses — log + flag", { x: 9.8, y: 3.7, w: 2.5, h: 0.8, fill: RED_BG, border: RED, color: RED, fontSize: 11 });

  box(s, "Core1 capture + Core2 HMAC/counter validation — runs unaffected the entire time", { x: 0.5, y: 5.1, w: 11.8, h: 0.9, fill: GREY_BG, border: GREY, color: INK, fontSize: 12 });
  caption(s, "Detects a hung Linux — never gates or weakens the existing CAN integrity guarantee", { fill: GREEN_BG, border: GREEN });
  addLogo(s); addFooter(s, 5, TOTAL);
}

p.writeFile({ fileName: "/home/aslam/Minor_Project/Secure_CAN_Flow_Diagrams_Only.pptx" }).then(() => {
  console.log("Deck written: Secure_CAN_Flow_Diagrams_Only.pptx");
});
