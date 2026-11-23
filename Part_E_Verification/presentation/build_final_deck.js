// Final presentation: Secure CAN Monitor (11 slides planned). This file builds the slides finished so far.
// Run (from this folder):  NODE_PATH=/home/aslam/Minor_Project/node_modules:<folder with react-icons, react, react-dom, sharp@0.32> node build_final_deck.js
// Style = the midterm deck (light grey, navy, blue accent, IIT Delhi logo). Few words per slide, a diagram on every slide.
const pptxgen = require("pptxgenjs");
const React = require("react");
const ReactDOMServer = require("react-dom/server");
const sharp = require("sharp");
const fa = require("react-icons/fa");
const path = require("path");
const { applyTheme } = require(path.join(process.env.PPTX_SKILL_DIR, "scripts/apply_theme.js"));

const OUT = path.join(__dirname, "Secure_CAN_Monitor_Final.pptx");
const LOGO = "/home/aslam/Minor_Project/midterm_deck_v2/html/IIT_Delhi_logo.png";

const THEME = {
  name: "Secure CAN Monitor",
  headFontFace: "Calibri",
  bodyFontFace: "Calibri",
  colors: {
    dk1: "0B1F3A", lt1: "FFFFFF", dk2: "44536A", lt2: "F2F4F7",
    accent1: "2F6FD0", accent2: "1F9D6B", accent3: "E8871E", accent4: "D64545", accent5: "7A8CA5", accent6: "5B6B84",
    hlink: "2F6FD0", folHlink: "7A8CA5",
  },
};

async function icon(Comp, color = "#FFFFFF", size = 256) {
  const svg = ReactDOMServer.renderToStaticMarkup(React.createElement(Comp, { color, size: String(size) }));
  const buf = await sharp(Buffer.from(svg)).png().toBuffer();
  return "image/png;base64," + buf.toString("base64");
}

async function main() {
  const pres = new pptxgen();
  pres.layout = "LAYOUT_WIDE"; // 13.33 x 7.5 in
  pres.title = "Secure CAN Monitor using ARM TrustZone and OP-TEE";
  pres.author = "Aslam";
  pres.theme = { headFontFace: THEME.headFontFace, bodyFontFace: THEME.bodyFontFace };
  const C = pres.SchemeColor;
  const K = THEME.colors; // hex values, only for options that take hex (shadow, chart colors)
  const shadow = () => ({ type: "outer", color: "000000", blur: 8, offset: 2, angle: 90, opacity: 0.12 });

  // ---------------------------------------------------------------- layouts (the frames)
  const logoObj = { image: { path: LOGO, x: 12.05, y: 0.3, w: 0.8, h: 0.8 } };
  pres.defineSlideMaster({
    title: "TITLE_LAYOUT", background: { color: K.lt2 },
    objects: [
      logoObj,
      { placeholder: { options: { name: "title", type: "title", x: 0.7, y: 1.75, w: 11.2, h: 1.8, fontFace: "Calibri", fontSize: 40, bold: true, color: C.text1, align: "left", valign: "top", margin: 0 }, text: "" } },
    ],
  });
  pres.defineSlideMaster({
    title: "CONTENT_LAYOUT", background: { color: K.lt2 },
    slideNumber: { x: 12.0, y: 7.05, w: 0.8, h: 0.3, fontFace: "Calibri", fontSize: 10, color: C.accent5, align: "right" },
    objects: [
      logoObj,
      { placeholder: { options: { name: "title", type: "title", x: 0.6, y: 0.4, w: 11.2, h: 0.8, fontFace: "Calibri", fontSize: 34, bold: true, color: C.text1, align: "left", valign: "middle", margin: 0 }, text: "" } },
      { text: { text: "Secure CAN Monitor  |  Minor Project  |  IIT Delhi", options: { x: 0.6, y: 7.05, w: 8, h: 0.3, fontFace: "Calibri", fontSize: 10, color: C.accent5, margin: 0 } } },
    ],
  });

  pres.defineSlideMaster({
    title: "DARK_LAYOUT", background: { color: K.dk1 },
    slideNumber: { x: 12.0, y: 7.05, w: 0.8, h: 0.3, fontFace: "Calibri", fontSize: 10, color: "8FA3BF", align: "right" },
    objects: [
      { placeholder: { options: { name: "title", type: "title", x: 0.7, y: 0.55, w: 8, h: 0.4, fontFace: "Calibri", fontSize: 14, bold: true, color: "6FA8FF", charSpacing: 3, align: "left", valign: "middle", margin: 0 }, text: "" } },
      { text: { text: "Secure CAN Monitor  |  Minor Project  |  IIT Delhi", options: { x: 0.6, y: 7.05, w: 8, h: 0.3, fontFace: "Calibri", fontSize: 10, color: "8FA3BF", margin: 0 } } },
    ],
  });

  // icons (rendered once)
  const ic = {
    net: await icon(fa.FaNetworkWired), car: await icon(fa.FaCar), laptop: await icon(fa.FaLaptopCode), shield: await icon(fa.FaShieldAlt),
    q: await icon(fa.FaQuestion), spy: await icon(fa.FaUserSecret), redo: await icon(fa.FaRedo), bolt: await icon(fa.FaBolt),
    tools: await icon(fa.FaTools), check: await icon(fa.FaCheck), search: await icon(fa.FaSearch), chip: await icon(fa.FaMicrochip), lock: await icon(fa.FaLock),
    netDark: await icon(fa.FaNetworkWired, "#2F6FD0"), lapDark: await icon(fa.FaLaptopCode, "#2F6FD0"),
    times: await icon(fa.FaTimes), excl: await icon(fa.FaExclamation), key: await icon(fa.FaKey), chipDark: await icon(fa.FaMicrochip, "#2F6FD0"),
  };
  const arrow = (slide, x1, y1, x2, y2, color, name) => {
    slide.addShape(pres.shapes.LINE, { x: Math.min(x1, x2), y: Math.min(y1, y2), w: Math.abs(x2 - x1), h: Math.abs(y2 - y1), flipV: y2 < y1, flipH: x2 < x1, line: { color, width: 2.25, endArrowType: "triangle" }, objectName: name });
  };
  const circleIcon = (slide, img, cx, cy, d, fill, name) => {
    slide.addShape(pres.shapes.OVAL, { x: cx - d / 2, y: cy - d / 2, w: d, h: d, fill: { color: fill }, line: { color: fill, width: 0 }, objectName: name + " circle" });
    slide.addImage({ data: img, x: cx - d * 0.27, y: cy - d * 0.27, w: d * 0.54, h: d * 0.54, altText: name });
  };

  // ================================================================ SLIDE 1: title
  pres.addSection({ title: "Introduction" });
  let s = pres.addSlide({ masterName: "TITLE_LAYOUT", sectionTitle: "Introduction" });
  s.addText("MINOR PROJECT  |  FINAL PRESENTATION", { x: 0.7, y: 1.2, w: 8, h: 0.4, fontFace: "Calibri", fontSize: 14, bold: true, color: C.accent1, charSpacing: 3, margin: 0, isTextBox: true });
  s.addText([
    { text: "Secure CAN Monitor", options: { color: C.text1, breakLine: true } },
    { text: "using ARM ", options: { color: C.text1 } },
    { text: "TrustZone", options: { color: C.accent1 } },
    { text: " and OP-TEE", options: { color: C.text1 } },
  ], { placeholder: "title" });
  s.addText("A monitor whose verdict Linux cannot change, tested on a real CAN-FD bus", { x: 0.7, y: 3.6, w: 11.4, h: 0.9, fontFace: "Calibri", fontSize: 20, color: C.text2, margin: 0, valign: "top", isTextBox: true });
  s.addText("Aslam", { x: 0.7, y: 5.55, w: 7, h: 0.5, fontFace: "Calibri", fontSize: 28, bold: true, color: C.text1, margin: 0, isTextBox: true });
  s.addText("Supervisor: Prof. Kolin Paul  |  Indian Institute of Technology Delhi", { x: 0.7, y: 6.1, w: 7.6, h: 0.4, fontFace: "Calibri", fontSize: 16, color: C.text2, margin: 0, isTextBox: true });
  s.addNotes("Title slide. One sentence for the idea: a CAN monitor where the judge is the Secure World of the processor (ARM TrustZone with OP-TEE), not Linux. Linux only carries the messages. Everything in this talk was measured on real hardware: two Raspberry Pi 3 boards on a real CAN-FD bus.");

  // ================================================================ SLIDE 2: problem statement (dark, one idea)
  s = pres.addSlide({ masterName: "DARK_LAYOUT", sectionTitle: "Introduction" });
  s.addText("PROBLEM STATEMENT", { placeholder: "title" });
  s.addShape(pres.shapes.OVAL, { x: 12.0, y: 0.3, w: 0.95, h: 0.95, fill: { color: C.background1 }, line: { color: C.background1, width: 0 }, objectName: "Logo badge" });
  s.addImage({ path: LOGO, x: 12.07, y: 0.37, w: 0.81, h: 0.81, altText: "IIT Delhi logo" });
  s.addText([
    { text: "A CAN message cannot prove", options: { breakLine: true } },
    { text: "who", options: { color: "FFB454" } },
    { text: " sent it." },
  ], { x: 0.7, y: 1.05, w: 11, h: 1.55, fontFace: "Calibri", fontSize: 44, bold: true, color: C.background1, valign: "top", margin: 0, isTextBox: true });
  // the message, drawn as a strip: ID | DATA | missing sender proof
  const sy = 2.85, sh = 1.05;
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 0.7, y: sy, w: 2.3, h: sh, rectRadius: 0.1, fill: { color: "1F4272" }, line: { color: "3A6199", width: 1 }, objectName: "Message ID" });
  s.addText([{ text: "ID", options: { fontSize: 14, color: "9DB4D3", breakLine: true } }, { text: "0x123", options: { fontSize: 24, bold: true, color: C.background1 } }], { x: 0.7, y: sy, w: 2.3, h: sh, align: "center", valign: "middle", fontFace: "Calibri", margin: 0, isTextBox: true });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 3.15, y: sy, w: 4.4, h: sh, rectRadius: 0.1, fill: { color: "1F4272" }, line: { color: "3A6199", width: 1 }, objectName: "Message data" });
  s.addText([{ text: "DATA", options: { fontSize: 14, color: "9DB4D3", breakLine: true } }, { text: "speed = 80 km/h", options: { fontSize: 24, bold: true, color: C.background1 } }], { x: 3.15, y: sy, w: 4.4, h: sh, align: "center", valign: "middle", fontFace: "Calibri", margin: 0, isTextBox: true });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 7.7, y: sy, w: 4.9, h: sh, rectRadius: 0.1, fill: { color: "FF6B6B", transparency: 85 }, line: { color: "FF6B6B", width: 2, dashType: "dash" }, objectName: "Missing sender proof" });
  circleIcon(s, ic.q, 8.4, sy + sh / 2, 0.7, "FF6B6B", "Missing sender proof");
  s.addText([{ text: "SENDER PROOF", options: { fontSize: 14, color: "FF9A9A", breakLine: true } }, { text: "missing", options: { fontSize: 24, bold: true, color: "FF9A9A" } }], { x: 9.0, y: sy, w: 3.4, h: sh, align: "left", valign: "middle", fontFace: "Calibri", margin: 0, isTextBox: true });
  s.addText("Any node on the bus can send any message.", { x: 0.7, y: sy + sh + 0.12, w: 8, h: 0.4, fontFace: "Calibri", fontSize: 16, color: "C9D6E8", margin: 0, isTextBox: true });
  // three attacks
  s.addText("THREE ATTACKS THAT FOLLOW", { x: 0.7, y: 4.5, w: 8, h: 0.3, fontFace: "Calibri", fontSize: 12, bold: true, color: "6FA8FF", charSpacing: 2, margin: 0, isTextBox: true });
  [["Forged", "a message nobody sent", ic.spy, "D64545"], ["Replayed", "an old message, sent again", ic.redo, "E8871E"], ["Flood", "too many to handle", ic.bolt, "C13584"]].forEach(([t, sub, img, col], i) => {
    const x = 0.7 + i * 4.05, y = 4.85;
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y, w: 3.8, h: 1.1, rectRadius: 0.12, fill: { color: "152E52" }, line: { color: "2A4a7a", width: 1 }, objectName: t + " tile" });
    circleIcon(s, img, x + 0.65, y + 0.55, 0.72, col, t);
    s.addText(t, { x: x + 1.2, y: y + 0.1, w: 2.5, h: 0.45, fontFace: "Calibri", fontSize: 20, bold: true, color: C.background1, valign: "middle", margin: 0, isTextBox: true });
    s.addText(sub, { x: x + 1.2, y: y + 0.55, w: 2.5, h: 0.5, fontFace: "Calibri", fontSize: 14, color: "C9D6E8", valign: "top", margin: 0, isTextBox: true });
  });
  // the answer
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 0.7, y: 6.2, w: 11.9, h: 0.68, rectRadius: 0.12, fill: { color: C.accent1 }, line: { color: C.accent1, width: 0 }, objectName: "Need banner" });
  s.addImage({ data: ic.shield, x: 0.95, y: 6.3, w: 0.48, h: 0.48, altText: "Shield" });
  s.addText("We need a judge that attackers cannot reach, not even through Linux.", { x: 1.65, y: 6.2, w: 10.8, h: 0.68, fontFace: "Calibri", fontSize: 20, bold: true, color: C.background1, valign: "middle", margin: 0, isTextBox: true });
  s.addNotes("Problem statement. A CAN message has an ID and data, but nothing in it proves who sent it: any node on the bus can send any message. From this follow three attacks: forged messages (nobody really sent them), replayed messages (an old valid message sent again) and floods (so many messages that the system chokes). Today the check is done by normal software, Linux, and Linux can be attacked. So we need a judge that attackers cannot reach, not even through Linux. That judge is the Secure World of the processor.");

  // ================================================================ SLIDE 3: midterm recap, the idea
  pres.addSection({ title: "Midterm recap" });
  s = pres.addSlide({ masterName: "CONTENT_LAYOUT", sectionTitle: "Midterm recap" });
  s.addText("Midterm recap: the idea", { placeholder: "title" });
  s.addText("Put the judge inside the processor, where Linux cannot reach.", { x: 0.6, y: 1.25, w: 11.2, h: 0.4, fontFace: "Calibri", fontSize: 18, color: C.text2, margin: 0, isTextBox: true });
  s.addText("HOW CAN PROCESSING IS PROTECTED", { x: 0.6, y: 1.95, w: 5.6, h: 0.3, fontFace: "Calibri", fontSize: 12, bold: true, color: C.accent1, charSpacing: 2, margin: 0, isTextBox: true });
  [
    ["Software only", "Linux does everything. Breaks if Linux is hacked.", ic.laptop, C.accent5, ic.times, C.accent4, false],
    ["External security chip", "A separate HSM. Extra hardware and cost.", ic.chip, C.accent5, ic.excl, C.accent3, false],
    ["TrustZone + OP-TEE", "Inside the processor. No extra hardware.", ic.shield, C.accent1, ic.check, C.accent2, true],
  ].forEach(([t, sub, img, fill, mark, markFill, ours], i) => {
    const x = 0.6, y = 2.35 + i * 1.22, w = 5.6, h = 1.1;
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y, w, h, rectRadius: 0.12, fill: ours ? { color: C.accent1, transparency: 90 } : { color: C.background1 }, line: { color: ours ? C.accent1 : C.accent5, width: ours ? 2 : 1 }, shadow: ours ? undefined : shadow(), objectName: t + " card" });
    circleIcon(s, img, x + 0.65, y + h / 2, 0.7, fill, t);
    s.addText(t, { x: x + 1.2, y: y + 0.08, w: 3.3, h: 0.4, fontFace: "Calibri", fontSize: 18, bold: true, color: C.text1, valign: "middle", margin: 0, isTextBox: true });
    s.addText(sub, { x: x + 1.2, y: y + 0.5, w: 3.3, h: 0.55, fontFace: "Calibri", fontSize: 14, color: C.text2, valign: "top", margin: 0, isTextBox: true });
    circleIcon(s, mark, x + w - 0.6, y + h / 2, 0.5, markFill, t + " verdict");
  });
  // our approach, as one clean picture
  s.addText("OUR APPROACH", { x: 6.9, y: 1.95, w: 5.5, h: 0.3, fontFace: "Calibri", fontSize: 12, bold: true, color: C.accent1, charSpacing: 2, margin: 0, isTextBox: true });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 8.15, y: 2.35, w: 3.0, h: 0.62, rectRadius: 0.12, fill: { color: C.background1 }, line: { color: C.accent5, width: 1 }, shadow: shadow(), objectName: "CAN chip box" });
  s.addImage({ data: ic.chipDark, x: 8.32, y: 2.5, w: 0.32, h: 0.32, altText: "CAN chip" });
  s.addText("CAN chip", { x: 8.78, y: 2.35, w: 2.3, h: 0.62, fontFace: "Calibri", fontSize: 18, bold: true, color: C.text1, valign: "middle", margin: 0, isTextBox: true });
  arrow(s, 9.65, 2.99, 9.65, 3.37, C.accent5, "Arrow chip to Secure World");
  s.addText("interrupt", { x: 9.8, y: 3.0, w: 1.6, h: 0.3, fontFace: "Calibri", fontSize: 12, italic: true, color: C.text2, margin: 0, isTextBox: true });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 6.9, y: 3.4, w: 5.5, h: 1.8, rectRadius: 0.12, fill: { color: C.text1 }, line: { color: C.text1, width: 1 }, shadow: shadow(), objectName: "Secure World container" });
  s.addText("Secure World  ·  Core 1", { x: 7.15, y: 3.46, w: 5.0, h: 0.4, fontFace: "Calibri", fontSize: 18, bold: true, color: C.background1, valign: "middle", margin: 0, isTextBox: true });
  ["ID", "Counter", "Time", "HMAC"].forEach((t, i) => {
    const x = 7.15 + i * 1.29;
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y: 3.95, w: 1.17, h: 0.5, rectRadius: 0.08, fill: { color: C.accent1 }, line: { color: C.accent1, width: 0 }, objectName: t + " check" });
    s.addText(t, { x, y: 3.95, w: 1.17, h: 0.5, fontFace: "Calibri", fontSize: 16, bold: true, color: C.background1, align: "center", valign: "middle", margin: 0, isTextBox: true });
  });
  [["ACCEPT", C.accent2, 7.95], ["DROP", C.accent4, 9.85]].forEach(([t, col, x]) => {
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y: 4.58, w: 1.5, h: 0.5, rectRadius: 0.25, fill: { color: col }, line: { color: col, width: 0 }, objectName: t + " pill" });
    s.addText(t, { x, y: 4.58, w: 1.5, h: 0.5, fontFace: "Calibri", fontSize: 14, bold: true, color: C.background1, align: "center", valign: "middle", margin: 0, isTextBox: true });
  });
  s.addShape(pres.shapes.LINE, { x: 6.6, y: 5.55, w: 6.1, h: 0, line: { color: C.accent4, width: 1.5, dashType: "dash" }, objectName: "TrustZone boundary" });
  s.addText("TrustZone boundary", { x: 10.2, y: 5.29, w: 2.2, h: 0.22, fontFace: "Calibri", fontSize: 10, color: C.accent4, align: "right", margin: 0, isTextBox: true });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 6.9, y: 5.68, w: 5.5, h: 0.45, rectRadius: 0.1, fill: { color: C.accent1, transparency: 88 }, line: { color: C.accent1, width: 1 }, objectName: "Linux box" });
  s.addText("Linux  ·  cores 0, 2, 3  ·  runs normally", { x: 6.9, y: 5.68, w: 5.5, h: 0.45, fontFace: "Calibri", fontSize: 14, color: C.text1, align: "center", valign: "middle", margin: 0, isTextBox: true });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 0.6, y: 6.25, w: 12.1, h: 0.62, rectRadius: 0.12, fill: { color: C.text1 }, line: { color: C.text1, width: 0 }, objectName: "Research gap banner" });
  s.addText([{ text: "Research gap: ", options: { bold: true } }, { text: "can one TrustZone core validate CAN traffic, with no extra hardware, even if Linux is hacked?" }], { x: 0.9, y: 6.25, w: 11.6, h: 0.62, fontFace: "Calibri", fontSize: 16, color: C.background1, valign: "middle", margin: 0, isTextBox: true });
  s.addNotes("Midterm recap, part 1: the idea. Three ways to protect CAN processing. Software only: Linux does everything, so a hacked Linux breaks it. An external security chip (HSM) isolates the check but adds hardware and cost. Our approach uses ARM TrustZone with OP-TEE: the CAN chip's interrupt goes to the Secure World on CPU core 1, which checks the ID, the message counter, the time and the HMAC signature, then accepts or drops. Linux keeps running on cores 0, 2 and 3. The research gap from the midterm: can one TrustZone core do this with no extra hardware, even if Linux is hacked?");

  // ================================================================ SLIDE 4: midterm recap, built by 24 September
  s = pres.addSlide({ masterName: "CONTENT_LAYOUT", sectionTitle: "Midterm recap" });
  s.addText("Midterm recap: built by 24 September", { placeholder: "title" });
  s.addText("Five pieces of work, seven milestones: three proven on hardware, one blocked.", { x: 0.6, y: 1.25, w: 11.2, h: 0.4, fontFace: "Calibri", fontSize: 18, color: C.text2, margin: 0, isTextBox: true });
  const steps = [
    ["Aug 23 - Sep 6", "Platform built", "TF-A, OP-TEE, U-Boot, Linux for the Pi 3", ic.tools],
    ["Sep 8", "OP-TEE runs", "36,114 tests pass; first own Trusted App", ic.check],
    ["Sep 16", "Signature check", "HMAC in a Trusted App rejects a changed byte", ic.key],
    ["Sep 16 - 17", "CAN chip alive", "MCP2518FD shield works as can0", ic.chip],
    ["Sep 17 - 18", "Secure ownership", "Secure World owns the chip and its interrupt", ic.lock],
  ];
  const colx = [0.85, 3.2, 5.55, 7.9, 10.25], ly = 2.7;
  s.addShape(pres.shapes.LINE, { x: 1.25, y: ly, w: 11.2, h: 0, line: { color: C.accent5, width: 3 }, objectName: "Timeline" });
  steps.forEach(([date, head, desc, img], i) => {
    const x = colx[i];
    s.addText(date, { x, y: 1.9, w: 2.2, h: 0.32, fontFace: "Calibri", fontSize: 14, bold: true, color: C.accent1, margin: 0, isTextBox: true });
    circleIcon(s, img, x + 0.4, ly, 0.8, C.accent1, head);
    s.addText(head, { x, y: 3.25, w: 2.2, h: 0.35, fontFace: "Calibri", fontSize: 16, bold: true, color: C.text1, margin: 0, isTextBox: true });
    s.addText(desc, { x, y: 3.62, w: 2.2, h: 0.95, fontFace: "Calibri", fontSize: 14, color: C.text2, valign: "top", margin: 0, isTextBox: true });
  });
  // the seven milestones of the midterm
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 0.6, y: 4.75, w: 12.1, h: 2.15, rectRadius: 0.12, fill: { color: C.background1 }, line: { color: C.accent5, width: 1 }, shadow: shadow(), objectName: "Milestone panel" });
  s.addText("3 of 7", { x: 0.85, y: 5.0, w: 2.6, h: 0.9, fontFace: "Calibri", fontSize: 48, bold: true, color: C.accent2, valign: "middle", margin: 0, isTextBox: true });
  s.addText("milestones proven on hardware", { x: 0.85, y: 5.9, w: 2.6, h: 0.7, fontFace: "Calibri", fontSize: 14, color: C.text2, valign: "top", margin: 0, isTextBox: true });
  const ms = [
    ["Boot & core pinning", "proven", C.accent2], ["Secure CAN ownership", "proven", C.accent2], ["ID, replay, timestamp", "proven", C.accent2],
    ["HMAC in the Core 1 loop", "blocked", C.accent3], ["Rate & flood detection", "not started", C.accent5], ["Tamper-evident log", "not started", C.accent5], ["Final demo & metrics", "not started", C.accent5],
  ];
  const mx0 = 3.95, mstep = 1.2, my = 5.3;
  for (let i = 0; i < ms.length - 1; i++) {
    s.addShape(pres.shapes.LINE, { x: mx0 + i * mstep + 0.3, y: my, w: mstep - 0.6, h: 0, line: { color: ms[i + 1][2] === C.accent5 ? C.accent5 : ms[i][2], width: 2.5, dashType: ms[i + 1][2] === C.accent5 ? "dash" : "solid" }, objectName: "Link " + (i + 1) });
  }
  ms.forEach(([t, stt, col], i) => {
    const cx = mx0 + i * mstep;
    s.addShape(pres.shapes.OVAL, { x: cx - 0.3, y: my - 0.3, w: 0.6, h: 0.6, fill: { color: col }, line: { color: col, width: 0 }, objectName: "Milestone " + (i + 1) });
    s.addText(String(i + 1), { x: cx - 0.3, y: my - 0.3, w: 0.6, h: 0.6, fontFace: "Calibri", fontSize: 16, bold: true, color: C.background1, align: "center", valign: "middle", margin: 0, isTextBox: true });
    s.addText(t, { x: cx - 0.58, y: my + 0.4, w: 1.16, h: 0.55, fontFace: "Calibri", fontSize: 12, bold: true, color: C.text1, align: "center", valign: "top", margin: 0, isTextBox: true });
    s.addText(stt, { x: cx - 0.58, y: my + 0.95, w: 1.16, h: 0.25, fontFace: "Calibri", fontSize: 11, color: col, align: "center", valign: "top", margin: 0, isTextBox: true });
  });
  s.addText("HMAC already worked in a Trusted App (16 Sep); inside the Core 1 loop it hit a runtime issue.", { x: 3.5, y: 6.5, w: 9.0, h: 0.28, fontFace: "Calibri", fontSize: 12, italic: true, color: C.text2, margin: 0, isTextBox: true });
  s.addNotes("Midterm recap, part 2: what was built by 24 September. Platform: Trusted Firmware-A, OP-TEE, U-Boot and Linux for the Raspberry Pi 3. OP-TEE passed 36,114 tests and our own Trusted App ran. On 16 September a Trusted App authenticated simulated CAN frames with HMAC-SHA256 and rejected a frame with one changed byte. The same days the CAN-FD shield (MCP2518FD) came up as can0. By 17 and 18 September the Secure World owned the CAN chip and its interrupt, and CPU core 1 was reserved for the Secure World. At the midterm 3 of 7 milestones were proven on hardware: boot and core pinning, secure CAN ownership, and the ID, replay and timestamp checks. HMAC authentication inside the Core 1 loop was blocked by a Secure World runtime issue (HMAC itself had worked in a Trusted App). Flood detection, the tamper-evident log and the final demo had not started.");

  await pres.writeFile({ fileName: OUT });
  await applyTheme(OUT, THEME);
  console.log("written", OUT);
}
main().catch((e) => { console.error(e); process.exit(1); });
