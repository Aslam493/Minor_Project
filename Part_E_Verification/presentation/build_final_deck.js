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
      { placeholder: { options: { name: "title", type: "title", x: 0.7, y: 1.75, w: 7.4, h: 2.1, fontFace: "Calibri", fontSize: 40, bold: true, color: C.text1, align: "left", valign: "top", margin: 0 }, text: "" } },
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

  // icons (rendered once)
  const ic = {
    net: await icon(fa.FaNetworkWired), car: await icon(fa.FaCar), laptop: await icon(fa.FaLaptopCode), shield: await icon(fa.FaShieldAlt),
    q: await icon(fa.FaQuestion), spy: await icon(fa.FaUserSecret), redo: await icon(fa.FaRedo), bolt: await icon(fa.FaBolt),
    tools: await icon(fa.FaTools), check: await icon(fa.FaCheck), search: await icon(fa.FaSearch), chip: await icon(fa.FaMicrochip), lock: await icon(fa.FaLock),
    netDark: await icon(fa.FaNetworkWired, "#2F6FD0"), lapDark: await icon(fa.FaLaptopCode, "#2F6FD0"),
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
    { text: "TrustZone", options: { color: C.accent1, breakLine: true } },
    { text: "and OP-TEE", options: { color: C.text1 } },
  ], { placeholder: "title" });
  s.addText("A monitor whose verdict Linux cannot change, tested on a real CAN-FD bus", { x: 0.7, y: 4.05, w: 7.2, h: 0.9, fontFace: "Calibri", fontSize: 20, color: C.text2, margin: 0, valign: "top", isTextBox: true });
  s.addText("Aslam", { x: 0.7, y: 5.55, w: 7, h: 0.5, fontFace: "Calibri", fontSize: 28, bold: true, color: C.text1, margin: 0, isTextBox: true });
  s.addText("Supervisor: Prof. Kolin Paul  |  Indian Institute of Technology Delhi", { x: 0.7, y: 6.1, w: 7.6, h: 0.4, fontFace: "Calibri", fontSize: 16, color: C.text2, margin: 0, isTextBox: true });
  // mini picture of the idea
  const bx = 8.85, bw = 3.9;
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: bx, y: 1.7, w: bw, h: 1.05, rectRadius: 0.12, fill: { color: C.background1 }, line: { color: C.accent5, width: 1 }, shadow: shadow(), objectName: "Bus box" });
  s.addImage({ data: ic.netDark, x: bx + 0.2, y: 1.98, w: 0.5, h: 0.5, altText: "CAN bus" });
  s.addText([{ text: "CAN-FD bus", options: { bold: true, fontSize: 20, color: C.text1, breakLine: true } }, { text: "real hardware", options: { fontSize: 14, color: C.text2 } }], { x: bx + 0.9, y: 1.78, w: 2.9, h: 0.9, valign: "middle", margin: 0, isTextBox: true });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: bx, y: 3.3, w: bw, h: 1.05, rectRadius: 0.12, fill: { color: C.accent1, transparency: 85 }, line: { color: C.accent1, width: 1 }, objectName: "Linux box" });
  s.addImage({ data: ic.lapDark, x: bx + 0.2, y: 3.58, w: 0.5, h: 0.5, altText: "Linux" });
  s.addText([{ text: "Linux", options: { bold: true, fontSize: 20, color: C.text1, breakLine: true } }, { text: "only the postman", options: { fontSize: 14, color: C.text2 } }], { x: bx + 0.9, y: 3.38, w: 2.9, h: 0.9, valign: "middle", margin: 0, isTextBox: true });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: bx, y: 4.95, w: bw, h: 1.2, rectRadius: 0.12, fill: { color: C.text1 }, line: { color: C.text1, width: 1 }, shadow: shadow(), objectName: "Secure World box" });
  s.addImage({ data: ic.shield, x: bx + 0.2, y: 5.3, w: 0.5, h: 0.5, altText: "Secure World" });
  s.addText([{ text: "Secure World", options: { bold: true, fontSize: 20, color: C.background1, breakLine: true } }, { text: "the judge (OP-TEE)", options: { fontSize: 14, color: C.background1 } }], { x: bx + 0.9, y: 5.02, w: 2.9, h: 1.05, valign: "middle", margin: 0, isTextBox: true });
  arrow(s, bx + 1.95, 2.78, bx + 1.95, 3.27, C.accent5, "Arrow bus to Linux");
  arrow(s, bx + 1.95, 4.38, bx + 1.95, 4.92, C.accent5, "Arrow Linux to Secure World");
  s.addShape(pres.shapes.LINE, { x: bx - 0.25, y: 4.66, w: bw + 0.5, h: 0, line: { color: C.accent4, width: 1.5, dashType: "dash" }, objectName: "TrustZone boundary" });
  s.addText("TrustZone boundary", { x: bx + 2.1, y: 4.38, w: 1.95, h: 0.25, fontFace: "Calibri", fontSize: 10, color: C.accent4, align: "right", margin: 0, isTextBox: true });
  s.addNotes("Title slide. One sentence for the idea: a CAN monitor where the judge is the Secure World of the processor (ARM TrustZone with OP-TEE), not Linux. Linux only carries the messages. Everything in this talk was measured on real hardware: two Raspberry Pi 3 boards on a real CAN-FD bus.");

  // ================================================================ SLIDE 2: problem statement
  s = pres.addSlide({ masterName: "CONTENT_LAYOUT", sectionTitle: "Introduction" });
  s.addText("Problem statement", { placeholder: "title" });
  s.addText("A CAN message carries no proof of who sent it, and the software that checks it can be attacked.", { x: 0.6, y: 1.25, w: 11.2, h: 0.5, fontFace: "Calibri", fontSize: 18, color: C.text2, margin: 0, isTextBox: true });
  const cy0 = 2.15, ch = 2.2;
  const card = (x, w, img, circleFill, head, body, opts) => {
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, Object.assign({ x, y: cy0, w, h: ch, rectRadius: 0.12, fill: { color: C.background1 }, line: { color: C.accent5, width: 1 }, shadow: shadow(), objectName: head + " card" }, opts || {}));
    circleIcon(s, img, x + 0.65, cy0 + 0.7, 0.8, circleFill, head);
    s.addText(head, { x: x + 0.3, y: cy0 + 1.2, w: w - 0.5, h: 0.4, fontFace: "Calibri", fontSize: 20, bold: true, color: C.text1, margin: 0, isTextBox: true });
    s.addText(body, { x: x + 0.3, y: cy0 + 1.6, w: w - 0.5, h: 0.45, fontFace: "Calibri", fontSize: 16, color: C.text2, margin: 0, isTextBox: true });
  };
  card(0.6, 3.6, ic.car, C.accent5, "CAN bus", "No proof of the sender");
  card(4.85, 3.6, ic.laptop, C.accent4, "Linux checks it", "Linux can be hacked", { fill: { color: C.accent4, transparency: 90 }, line: { color: C.accent4, width: 1.5 } });
  card(9.1, 3.6, ic.q, C.accent1, "Accept or drop?", "Can we trust the verdict?");
  arrow(s, 4.25, cy0 + ch / 2, 4.8, cy0 + ch / 2, C.accent5, "Arrow bus to Linux");
  arrow(s, 8.5, cy0 + ch / 2, 9.05, cy0 + ch / 2, C.accent5, "Arrow Linux to verdict");
  s.addText("ATTACKS TO DETECT", { x: 0.6, y: 4.6, w: 6, h: 0.3, fontFace: "Calibri", fontSize: 12, bold: true, color: C.accent1, charSpacing: 2, margin: 0, isTextBox: true });
  [["Forged message", ic.spy], ["Replayed message", ic.redo], ["Message flood", ic.bolt]].forEach(([t, img], i) => {
    const x = 0.6 + i * 4.15;
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y: 4.95, w: 3.8, h: 0.85, rectRadius: 0.12, fill: { color: C.background1 }, line: { color: C.accent5, width: 1 }, objectName: t + " chip" });
    circleIcon(s, img, x + 0.55, 5.375, 0.55, C.accent4, t);
    s.addText(t, { x: x + 1.05, y: 5.0, w: 2.7, h: 0.75, fontFace: "Calibri", fontSize: 18, bold: true, color: C.text1, valign: "middle", margin: 0, isTextBox: true });
  });
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: 0.6, y: 6.1, w: 12.1, h: 0.75, rectRadius: 0.12, fill: { color: C.text1 }, line: { color: C.text1, width: 1 }, objectName: "Goal banner" });
  s.addText([{ text: "Goal: ", options: { bold: true } }, { text: "detect these attacks in the Secure World, where Linux cannot reach, and report them." }], { x: 0.9, y: 6.1, w: 11.6, h: 0.75, fontFace: "Calibri", fontSize: 18, color: C.background1, valign: "middle", margin: 0, isTextBox: true });
  s.addNotes("Problem statement. CAN has no sender authentication: any node on the bus can send any message. Today the check is done by normal software (Linux). If an attacker controls Linux, the verdict cannot be trusted. We want the verdict to come from a place Linux cannot read or change. Three attacks we care about: forged messages, replayed (old) messages, and floods that overload the system.");

  // ================================================================ SLIDE 3: before the midterm
  pres.addSection({ title: "Midterm recap" });
  s = pres.addSlide({ masterName: "CONTENT_LAYOUT", sectionTitle: "Midterm recap" });
  s.addText("Before the midterm: what was built", { placeholder: "title" });
  s.addText("23 August to 24 September 2026", { x: 0.6, y: 1.25, w: 11.2, h: 0.4, fontFace: "Calibri", fontSize: 18, color: C.text2, margin: 0, isTextBox: true });
  const steps = [
    ["Aug 23 - Sep 6", "Platform built", "TF-A, OP-TEE, U-Boot, Linux for the Pi 3", ic.tools],
    ["Sep 8", "OP-TEE runs", "Tests pass; first own Trusted App", ic.check],
    ["Sep 14 - 16", "Secure checks", "Simulated CAN frames checked in the Secure World", ic.search],
    ["Sep 16", "Real CAN hardware", "CAN-FD shield (MCP2518FD), can0 works", ic.chip],
    ["Sep 17 - 18", "Secure ownership", "Secure World owns the CAN chip and its interrupt", ic.lock],
  ];
  const colx = [0.85, 3.2, 5.55, 7.9, 10.25], ly = 2.95;
  s.addShape(pres.shapes.LINE, { x: 1.25, y: ly, w: 11.2, h: 0, line: { color: C.accent5, width: 3 }, objectName: "Timeline" });
  steps.forEach(([date, head, desc, img], i) => {
    const x = colx[i];
    s.addText(date, { x, y: 1.95, w: 2.2, h: 0.35, fontFace: "Calibri", fontSize: 14, bold: true, color: C.accent1, margin: 0, isTextBox: true });
    circleIcon(s, img, x + 0.4, ly, 0.8, i === 4 ? C.text1 : C.accent1, head);
    s.addText(head, { x, y: 3.55, w: 2.2, h: 0.4, fontFace: "Calibri", fontSize: 16, bold: true, color: C.text1, margin: 0, isTextBox: true });
    s.addText(desc, { x, y: 3.98, w: 2.2, h: 1.25, fontFace: "Calibri", fontSize: 14, color: C.text2, valign: "top", margin: 0, isTextBox: true });
  });
  [["36,114", "OP-TEE tests passed"], ["Core 1", "kept for the Secure World"], ["0x123", "the one CAN id accepted"]].forEach(([big, lab], i) => {
    const x = 0.85 + i * 3.9;
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y: 5.6, w: 3.7, h: 1.2, rectRadius: 0.12, fill: { color: C.background1 }, line: { color: C.accent5, width: 1 }, shadow: shadow(), objectName: big + " stat" });
    s.addText(big, { x: x + 0.2, y: 5.7, w: 1.8, h: 1.0, fontFace: "Calibri", fontSize: 32, bold: true, color: C.accent1, valign: "middle", margin: 0, isTextBox: true });
    s.addText(lab, { x: x + 2.0, y: 5.7, w: 1.55, h: 1.0, fontFace: "Calibri", fontSize: 14, color: C.text2, valign: "middle", margin: 0, isTextBox: true });
  });
  s.addNotes("Work before the midterm (23 August to 24 September). First the platform: Trusted Firmware-A, OP-TEE, U-Boot, Linux and the root filesystem for the Raspberry Pi 3. Then OP-TEE ran and passed 36,114 tests, and our own Trusted App worked. Then simulated CAN frames were checked in the Secure World. Then the real CAN-FD shield (MCP2518FD) came up as can0. Last, the Secure World took ownership of the CAN chip: it reads the chip over SPI and the chip's interrupt arrives in the Secure World. CPU core 1 is reserved for the Secure World, Linux runs on the other three cores.");

  // ================================================================ SLIDE 4: midterm review
  s = pres.addSlide({ masterName: "CONTENT_LAYOUT", sectionTitle: "Midterm recap" });
  s.addText("Midterm review: where the project stood", { placeholder: "title" });
  s.addText("Status on 24 September: the checks worked on real hardware, authentication was the blocker.", { x: 0.6, y: 1.25, w: 11.2, h: 0.4, fontFace: "Calibri", fontSize: 18, color: C.text2, margin: 0, isTextBox: true });
  const st = {
    green: { fill: K.accent2, txt: "HARDWARE-PROVEN" }, orange: { fill: K.accent3, txt: "IN PROGRESS" }, grey: { fill: K.accent5, txt: "NOT STARTED" }, input: { fill: K.accent6, txt: "INPUT" },
  };
  const flow = [
    ["CAN frame", "from the MCP2518FD chip", st.input], ["Secure FIQ", "interrupt reaches Core 1", st.green],
    ["Frame checks", "ID, counter, timestamp", st.green], ["HMAC check", "runtime issue open", st.orange], ["Secure log", "tamper-evident", st.grey],
  ];
  const fw = 2.15, fg = 0.395, fy = 1.95, fh = 1.55;
  flow.forEach(([head, sub, stt], i) => {
    const x = 0.5 + i * (fw + fg);
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y: fy, w: fw, h: fh, rectRadius: 0.12, fill: { color: C.background1 }, line: { color: stt.fill, width: 2 }, shadow: shadow(), objectName: head + " box" });
    s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x: x + 0.15, y: fy + 0.12, w: fw - 0.3, h: 0.3, rectRadius: 0.08, fill: { color: stt.fill }, line: { color: stt.fill, width: 0 }, objectName: head + " status" });
    s.addText(stt.txt, { x: x + 0.15, y: fy + 0.12, w: fw - 0.3, h: 0.3, fontFace: "Calibri", fontSize: 11, bold: true, color: C.background1, align: "center", valign: "middle", margin: 0, isTextBox: true });
    s.addText(head, { x: x + 0.12, y: fy + 0.5, w: fw - 0.24, h: 0.5, fontFace: "Calibri", fontSize: 18, bold: true, color: C.text1, align: "center", valign: "middle", margin: 0, isTextBox: true });
    s.addText(sub, { x: x + 0.12, y: fy + 1.0, w: fw - 0.24, h: 0.45, fontFace: "Calibri", fontSize: 14, color: C.text2, align: "center", valign: "top", margin: 0, isTextBox: true });
    if (i < flow.length - 1) arrow(s, x + fw + 0.03, fy + fh / 2, x + fw + fg - 0.03, fy + fh / 2, C.accent5, "Arrow " + head);
  });
  s.addChart(pres.charts.DOUGHNUT, [{ name: "Milestones", labels: ["Proven", "In progress", "Not started"], values: [3, 1, 3] }], {
    x: 0.6, y: 3.8, w: 5.0, h: 3.1, holeSize: 55, chartColors: [K.accent2, K.accent3, K.accent5], showLegend: true, legendPos: "r", legendFontSize: 14, legendColor: K.dk2, legendFontFace: "+mn-lt",
    showValue: true, showPercent: false, dataLabelColor: "FFFFFF", dataLabelFontSize: 16, dataLabelFontBold: true, dataLabelFontFace: "+mn-lt",
    showTitle: true, title: "7 milestones at the midterm", titleFontSize: 16, titleColor: K.dk1, titleFontFace: "+mn-lt", dataBorder: { pt: 2, color: "F2F4F7" },
  });
  s.addText("Promised for the final presentation", { x: 6.2, y: 3.85, w: 6.5, h: 0.4, fontFace: "Calibri", fontSize: 18, bold: true, color: C.text1, margin: 0, isTextBox: true });
  ["HMAC authentication", "Faster validation (latency)", "Stored, tamper-evident log", "Flood detection", "End-to-end demo with real numbers"].forEach((t, i) => {
    const y = 4.4 + i * 0.5;
    s.addShape(pres.shapes.OVAL, { x: 6.2, y, w: 0.38, h: 0.38, fill: { color: C.accent1 }, line: { color: C.accent1, width: 0 }, objectName: "Number " + (i + 1) });
    s.addText(String(i + 1), { x: 6.2, y, w: 0.38, h: 0.38, fontFace: "Calibri", fontSize: 14, bold: true, color: C.background1, align: "center", valign: "middle", margin: 0, isTextBox: true });
    s.addText(t, { x: 6.75, y: y - 0.02, w: 5.9, h: 0.42, fontFace: "Calibri", fontSize: 16, color: C.text1, valign: "middle", margin: 0, isTextBox: true });
  });
  s.addNotes("Midterm review. At the midterm the path from the CAN chip to the Secure World worked on real hardware: the interrupt reached Core 1 as a Secure FIQ, and the ID, counter and timestamp checks were proven. HMAC authentication was blocked by a runtime problem in the Secure World. Flood detection, the tamper-evident log and the final demo had not started: 3 of 7 milestones proven, 1 in progress, 3 not started. The five promises on the right are what the rest of this talk answers, one by one.");

  await pres.writeFile({ fileName: OUT });
  await applyTheme(OUT, THEME);
  console.log("written", OUT);
}
main().catch((e) => { console.error(e); process.exit(1); });
