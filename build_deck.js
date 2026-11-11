const pptxgen = require("pptxgenjs");

const LOGO = "/home/aslam/Minor_Project/midterm_deck_v2/html/IIT_Delhi_logo.png";

// ---- palette ----
const NAVY = "0B1F3A";
const BLUE = "2A78D6";
const BLUE_LT = "5FA8FF";
const GREEN = "1E8E3E";
const GREEN_BG = "E6F4EA";
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
  slide.addText("Secure CAN Event Processing and Logging", {
    x: 0.5, y: 7.12, w: 6, h: 0.3, fontFace: FONT, fontSize: 10, color, isTextBox: true, margin: 0,
  });
  slide.addText(`${pageNum} / ${total}`, {
    x: 12.0, y: 7.12, w: 0.9, h: 0.3, fontFace: FONT, fontSize: 10, color, align: "right", isTextBox: true, margin: 0,
  });
}

function addHeader(slide, kicker, title, subtitle, kickerColor = BLUE) {
  slide.addText(kicker.toUpperCase(), {
    x: 0.5, y: 0.32, w: 8, h: 0.3, fontFace: FONT, fontSize: 13, bold: true, color: kickerColor,
    charSpacing: 2, isTextBox: true, margin: 0,
  });
  slide.addShape("line", { x: 0.5, y: 0.62, w: 11.4, h: 0, line: { color: NAVY, width: 1.2 } });
  slide.addText(title, {
    x: 0.5, y: 0.74, w: 11.6, h: 0.62, fontFace: FONT, fontSize: 30, bold: true, color: NAVY, isTextBox: true, margin: 0,
  });
  if (subtitle) {
    slide.addText(subtitle, {
      x: 0.5, y: 1.32, w: 11.8, h: 0.4, fontFace: FONT, fontSize: 14, color: SUBTLE, isTextBox: true, margin: 0,
    });
  }
}

const TOTAL_SLIDES = 9;
const p = pres();

// ============================================================= SLIDE 1 — TITLE
{
  const s = p.addSlide();
  s.background = { color: "F2F3F5" };
  s.addText("MINOR PROJECT  |  MIDTERM PRESENTATION", {
    x: 0.7, y: 1.9, w: 8, h: 0.35, fontFace: FONT, fontSize: 14, bold: true, color: BLUE,
    charSpacing: 2, isTextBox: true, margin: 0,
  });
  s.addText(
    [
      { text: "Secure CAN Gateway using ARM ", options: { color: NAVY } },
      { text: "TrustZone", options: { color: BLUE } },
      { text: " and OP-TEE", options: { color: NAVY } },
    ],
    { x: 0.7, y: 2.5, w: 11.8, h: 1.3, fontFace: FONT, fontSize: 40, bold: true, isTextBox: true, margin: 0, lineSpacing: 46 }
  );
  s.addShape("line", { x: 0.72, y: 3.95, w: 1.0, h: 0, line: { color: BLUE, width: 3 } });
  s.addText("Secure CAN Ownership, Validation and Event Logging on Raspberry Pi 3", {
    x: 0.7, y: 4.1, w: 10.5, h: 0.5, fontFace: FONT, fontSize: 18, color: SUBTLE, isTextBox: true, margin: 0,
  });
  s.addText("Aslam", {
    x: 0.7, y: 6.05, w: 4, h: 0.45, fontFace: FONT, fontSize: 24, bold: true, color: NAVY, isTextBox: true, margin: 0,
  });
  s.addText("Supervisor: Prof. Kolin Paul  |  Indian Institute of Technology Delhi", {
    x: 0.7, y: 6.55, w: 9, h: 0.4, fontFace: FONT, fontSize: 16, color: SUBTLE, isTextBox: true, margin: 0,
  });
  s.addImage({ path: LOGO, x: 11.9, y: 0.45, w: 0.85, h: 0.85 });
  addFooter(s, 1, TOTAL_SLIDES, false);
}

// ============================================================= SLIDE 2 — MOTIVATION
{
  const s = p.addSlide();
  s.background = { color: "F2F3F5" };
  addHeader(s, "MOTIVATION", "Can We Trust the CAN Event Throughout Its Lifecycle?",
    "From receiving a CAN packet to making a security decision and storing the resulting event, each stage introduces a trust concern.");
  addLogo(s);

  const cards = [
    {
      num: "01", color: BLUE, bg: BG_LIGHT, border: CARD_BORDER, title: "CAN MESSAGE TRUST",
      q: "Can We Trust the CAN Message?",
      steps: ["ECU A  ↔  CAN  ↔  ECU B", "ID  |  Counter  |  Data"],
      bullets: [
        "Communication: CAN enables ECUs to exchange control and monitoring messages.",
        "Security concern: The CAN frame itself does not provide cryptographic origin authentication.",
      ],
      trust: "TRUST ?",
    },
    {
      num: "02", color: ORANGE, bg: BG_LIGHT, border: CARD_BORDER, title: "CAN PROCESSING TRUST",
      q: "Can We Trust the Validation Decision?",
      steps: ["CAN PACKET", "ID | Counter | Data", "Linux / Normal World\n(Process + Validate)", "ACCEPT / DROP"],
      bullets: [
        "Processing: The received CAN packet is handled and validated by Linux / Normal World software.",
        "Trust concern: If the processing environment is compromised, can we trust the validation result?",
      ],
      trust: "Can we trust the security decision?",
    },
    {
      num: "03", color: "6B4423", bg: BG_LIGHT, border: CARD_BORDER, title: "LOG INTEGRITY",
      q: "Can We Trust the Record Later?",
      steps: ["VALIDATED EVENT", "ID | Counter | Time | Result", "Store in Memory\n(Security Log)", "Later / Analysis"],
      bullets: [
        "Storage: After validation, the CAN event is stored as a security record.",
        "Integrity concern: If the storage environment is compromised, can the recorded event be modified or erased?",
      ],
      trust: "Can we trust this record later?",
    },
  ];

  const cardW = 3.95, gap = 0.2, startX = 0.5, cardY = 1.7, cardH = 4.75;
  const stepsZoneTop = cardY + 1.2, stepsZoneH = 1.85;
  const bulletsY = cardY + 3.12, bulletsH = 1.0;
  const trustH = 0.55;
  cards.forEach((c, i) => {
    const x = startX + i * (cardW + gap);
    s.addShape("roundRect", { x, y: cardY, w: cardW, h: cardH, rectRadius: 0.08, fill: { color: c.bg }, line: { color: c.border, width: 1.2 } });
    s.addShape("ellipse", { x: x + 0.2, y: cardY + 0.18, w: 0.42, h: 0.42, fill: { color: c.color }, line: { type: "none" } });
    s.addText(c.num, { x: x + 0.2, y: cardY + 0.18, w: 0.42, h: 0.42, fontFace: FONT, fontSize: 14, bold: true, color: "FFFFFF", align: "center", valign: "middle", isTextBox: true, margin: 0 });
    s.addText(c.title, { x: x + 0.72, y: cardY + 0.2, w: cardW - 0.9, h: 0.4, fontFace: FONT, fontSize: 13, bold: true, color: NAVY, valign: "middle", isTextBox: true, margin: 0 });

    s.addShape("roundRect", { x: x + 0.18, y: cardY + 0.72, w: cardW - 0.36, h: 0.4, rectRadius: 0.06, fill: { color: "FFFFFF" }, line: { color: c.border, width: 1 } });
    s.addText(c.q, { x: x + 0.22, y: cardY + 0.72, w: cardW - 0.44, h: 0.4, fontFace: FONT, fontSize: 10.5, bold: true, color: "3A2E22", align: "center", valign: "middle", isTextBox: true, margin: 0 });

    // vertical step flow, single light-blue box with all steps stacked inside
    s.addShape("roundRect", { x: x + 0.18, y: stepsZoneTop, w: cardW - 0.36, h: stepsZoneH, rectRadius: 0.06, fill: { color: "D6E8FB" }, line: { color: "B7D6F5", width: 1 } });
    const compact = c.steps.length > 2;
    const stepFS = compact ? 10.5 : 12, arrowFS = compact ? 17 : 22, lnSp = compact ? 13 : 16;
    const flowRuns = [];
    c.steps.forEach((step, si) => {
      flowRuns.push({ text: step, options: { bold: true, fontSize: stepFS, breakLine: true } });
      if (si < c.steps.length - 1) {
        flowRuns.push({ text: "↓", options: { bold: true, fontSize: arrowFS, color: c.color, breakLine: true } });
      }
    });
    s.addText(flowRuns, {
      x: x + 0.24, y: stepsZoneTop + 0.1, w: cardW - 0.48, h: stepsZoneH - 0.1, fontFace: FONT, color: "16324F",
      align: "center", valign: "top", isTextBox: true, margin: 0, lineSpacing: lnSp,
    });

    const bulletRuns = [];
    c.bullets.forEach((b, bi) => {
      bulletRuns.push({ text: "•  ", options: { bold: true, color: c.color } });
      bulletRuns.push({ text: b, options: { breakLine: true } });
      if (bi < c.bullets.length - 1) bulletRuns.push({ text: "", options: { breakLine: true } });
    });
    s.addText(bulletRuns, {
      x: x + 0.2, y: bulletsY, w: cardW - 0.4, h: bulletsH, fontFace: FONT, fontSize: 10, color: "33363D",
      valign: "top", isTextBox: true, margin: 0, lineSpacing: 12,
    });

    s.addShape("roundRect", { x: x + 0.18, y: cardY + cardH - 0.62, w: cardW - 0.36, h: 0.48, rectRadius: 0.06, fill: { color: "FCEAE7" }, line: { color: "D9695B", width: 1 } });
    s.addText(c.trust, { x: x + 0.22, y: cardY + cardH - 0.62, w: cardW - 0.44, h: 0.48, fontFace: FONT, fontSize: 10.5, bold: true, color: "C0392B", align: "center", valign: "middle", isTextBox: true, margin: 0 });
  });

  s.addShape("roundRect", { x: 0.5, y: 6.55, w: 12.3, h: 0.6, rectRadius: 0.08, fill: { color: NAVY }, line: { type: "none" } });
  s.addText(
    [
      { text: "THE RESEARCH GAP:  ", options: { bold: true, color: BLUE_LT } },
      { text: "Security-critical CAN events need trustworthy processing and trustworthy records, even when the Normal World may be compromised.", options: { color: "F5E9D8" } },
    ],
    { x: 0.7, y: 6.55, w: 11.9, h: 0.6, fontFace: FONT, fontSize: 11, valign: "middle", isTextBox: true, margin: 0 }
  );
  addFooter(s, 2, TOTAL_SLIDES, false);
}

// ============================================================= SLIDE 3 — STATE OF THE ART
{
  const s = p.addSlide();
  s.background = { color: "F2F3F5" };
  addHeader(s, "STATE OF THE ART", "How Is Trustworthy CAN Handling Done Today?",
    "Different approaches protect different parts of the system, but real trade-offs remain.");
  addLogo(s);

  const cols = [
    {
      num: "01", color: GREY, title: "Software-Only Logging", desc: "Validation and logging in the same OS",
      steps: ["CAN Packet", "Validate + Log\n(Linux)", "Log File"],
      good: ["Simple, no extra hardware"],
      bad: ["Vulnerable if OS is compromised", "No hardware isolation"],
      protects: "Protects: None (software-dependent)",
    },
    {
      num: "02", color: BLUE, title: "External HSM / Logger", desc: "Security moved to separate hardware",
      steps: ["CAN Packet", "External HSM / ECU", "Log"],
      good: ["Isolates security from main ECU"],
      bad: ["Extra hardware, wiring and cost", "Added system complexity"],
      protects: "Protects: Security record (extra hardware)",
    },
    {
      num: "03", color: GREEN, title: "TrustZone + OP-TEE (Ours)", desc: "Secure-World-owned validation on-chip",
      steps: ["CAN Packet", "Secure World\n(Core1)", "Accept / Drop"],
      good: ["No extra silicon - uses on-chip TrustZone", "ID / replay / timestamp checks hardware-proven"],
      bad: ["HMAC authentication currently blocked", "Tamper-evident logging not yet implemented"],
      protects: "Protects: Validation decision (logging planned, not built)",
    },
  ];

  const cardW = 3.95, gap = 0.2, startX = 0.5, cardY = 1.7, cardH = 4.75;
  const flowZoneTop = cardY + 0.82, flowZoneH = 1.75;
  cols.forEach((c, i) => {
    const x = startX + i * (cardW + gap);
    s.addShape("roundRect", { x, y: cardY, w: cardW, h: cardH, rectRadius: 0.08, fill: { color: BG_LIGHT }, line: { color: CARD_BORDER, width: 1.2 } });
    s.addShape("ellipse", { x: x + 0.2, y: cardY + 0.18, w: 0.4, h: 0.4, fill: { color: c.color }, line: { type: "none" } });
    s.addText(c.num, { x: x + 0.2, y: cardY + 0.18, w: 0.4, h: 0.4, fontFace: FONT, fontSize: 14, bold: true, color: "FFFFFF", align: "center", valign: "middle", isTextBox: true, margin: 0 });
    s.addText(c.title, { x: x + 0.7, y: cardY + 0.1, w: cardW - 0.9, h: 0.32, fontFace: FONT, fontSize: 15, bold: true, color: NAVY, isTextBox: true, margin: 0 });
    s.addText(c.desc, { x: x + 0.7, y: cardY + 0.43, w: cardW - 0.9, h: 0.3, fontFace: FONT, fontSize: 10.5, color: SUBTLE, isTextBox: true, margin: 0 });

    // single light-blue vertical flow box, matching slide 2 style
    s.addShape("roundRect", { x: x + 0.18, y: flowZoneTop, w: cardW - 0.36, h: flowZoneH, rectRadius: 0.06, fill: { color: "D6E8FB" }, line: { color: "B7D6F5", width: 1 } });
    const flowRuns = [];
    c.steps.forEach((step, si) => {
      flowRuns.push({ text: step, options: { bold: true, fontSize: 13, breakLine: true } });
      if (si < c.steps.length - 1) {
        flowRuns.push({ text: "↓", options: { bold: true, fontSize: 22, color: c.color, breakLine: true } });
      }
    });
    s.addText(flowRuns, {
      x: x + 0.24, y: flowZoneTop + 0.08, w: cardW - 0.48, h: flowZoneH - 0.08, fontFace: FONT, color: "16324F",
      align: "center", valign: "top", isTextBox: true, margin: 0, lineSpacing: 17,
    });

    const pointRuns = [];
    c.good.forEach((g, gi) => {
      pointRuns.push({ text: "•  ", options: { bold: true, color: GREEN } });
      pointRuns.push({ text: g, options: { breakLine: true } });
    });
    c.bad.forEach((b) => {
      pointRuns.push({ text: "•  ", options: { bold: true, color: "C0392B" } });
      pointRuns.push({ text: b, options: { breakLine: true } });
    });
    s.addText(pointRuns, {
      x: x + 0.2, y: flowZoneTop + flowZoneH + 0.15, w: cardW - 0.4, h: cardH - flowZoneH - 0.8 - (flowZoneTop - cardY),
      fontFace: FONT, fontSize: 11.5, color: "33363D", valign: "top", isTextBox: true, margin: 0, lineSpacing: 16,
    });

    s.addShape("roundRect", { x: x + 0.18, y: cardY + cardH - 0.52, w: cardW - 0.36, h: 0.4, rectRadius: 0.05, fill: { color: "FFFFFF" }, line: { color: CARD_BORDER, width: 1 } });
    s.addText(c.protects, { x: x + 0.2, y: cardY + cardH - 0.52, w: cardW - 0.4, h: 0.4, fontFace: FONT, fontSize: 10.5, bold: true, color: "22262E", align: "center", valign: "middle", isTextBox: true, margin: 0 });
  });

  s.addShape("roundRect", { x: 0.5, y: 6.55, w: 12.3, h: 0.6, rectRadius: 0.08, fill: { color: "FDECEC" }, line: { color: "E8A6A0", width: 1.2 } });
  s.addText(
    [
      { text: "RESEARCH GAP:  ", options: { bold: true, color: "B8352B" } },
      { text: "Can a single dedicated core running inside TrustZone deliver trusted, scalable CAN validation and tamper-evident logging, without dedicated security hardware, even when the Normal World is compromised?", options: { color: "3A2E22" } },
    ],
    { x: 0.7, y: 6.55, w: 11.9, h: 0.6, fontFace: FONT, fontSize: 10, valign: "middle", isTextBox: true, margin: 0 }
  );
  addFooter(s, 3, TOTAL_SLIDES, false);
}

// ============================================================= SLIDE 4 — PROPOSED APPROACH (reference image, placed as-is)
{
  const s = p.addSlide();
  s.background = { color: "FFFFFF" };
  const imgW = 11.25, imgH = 7.5, imgX = (13.333 - imgW) / 2, imgY = 0;
  s.addImage({ path: "/home/aslam/Minor_Project/midterm_deck_v2/refs/slide4_approach.png", x: imgX, y: imgY, w: imgW, h: imgH });
  s.addImage({ path: LOGO, x: 12.55, y: 0.28, w: 0.55, h: 0.55 });
}


// ============================================================= SLIDE 5 — BOOT PROCESS FLOW (3-section redesign)
{
  const s = p.addSlide();
  s.background = { color: "FFFFFF" };

  const bNAVY = "102A43", bBLUE = "2F80ED", bGREEN = "27AE60", bAMBER = "F2C94C", bRED = "EB5757";
  const bLBLUE = "EAF3FF", bLGREEN = "EAF8F0", bLRED = "FDECEC", bLAMBER = "FFF7DF", bGRAY = "4A5568";

  s.addText("BOOT PROCESS FLOW", { x: 0.4, y: 0.14, w: 8.5, h: 0.4, fontFace: FONT, fontSize: 25, bold: true, color: bNAVY, isTextBox: true, margin: 0 });
  s.addText("Raspberry Pi 3 + ARM TrustZone + OP-TEE — Core1 Secure World", { x: 0.4, y: 0.54, w: 9.5, h: 0.26, fontFace: FONT, fontSize: 11.5, color: bGRAY, isTextBox: true, margin: 0 });
  addLogo(s);

  // ================= SECTION 1 — BOOT CHAIN =================
  const s1Y = 0.85, s1H = 1.87;
  const cardH = s1H - 0.25;

  // renders a vertical mini-flow of steps inside a box, with bold down-arrows, using the box's available height
  function vFlow(x, y, w, h, steps, color, fs) {
    const n = steps.length;
    const unitH = (fs + 4) / 72;
    const lineCounts = steps.map((t) => t.split("\n").length);
    const totalLines = lineCounts.reduce((a, b) => a + b, 0);
    const gapH = Math.max(0.06, (h - totalLines * unitH) / (n - 1 || 1));
    let sy = y;
    steps.forEach((t, i) => {
      const stepH = lineCounts[i] * unitH;
      s.addText(t, { x, y: sy, w, h: stepH, fontFace: FONT, fontSize: fs, bold: true, color, align: "center", valign: "middle", isTextBox: true, margin: 0, lineSpacing: fs + 1 });
      sy += stepH;
      if (i < n - 1) {
        s.addText("↓", { x, y: sy, w, h: gapH, fontFace: FONT, fontSize: 13, bold: true, color, align: "center", valign: "middle", isTextBox: true, margin: 0 });
        sy += gapH;
      }
    });
  }

  const cards = [
    { title: "POWER ON", steps: ["Raspberry Pi 3 SoC", "Boot ROM", "VideoCore"], note: "Reads first FAT32 boot partition", fill: bLBLUE, border: bBLUE, tc: bNAVY, w: 1.95 },
    { title: "GPU BOOT", steps: ["bootcode.bin", "start.elf + fixup*.dat", "config.txt"], note: "Loads boot files from SD card", fill: bLBLUE, border: bBLUE, tc: bNAVY, w: 1.95 },
    { title: "ARMSTUB + FIP", steps: ["armstub8.bin\n@ 0x00000000", "Padded TF-A BL1\n+\nfip.bin"], note: "ARM stub contains BL1 + FIP", fill: bLBLUE, border: bBLUE, tc: bNAVY, w: 1.95 },
  ];
  let cx = 0.4;
  const gapArrow = 0.3;
  cards.forEach((c) => {
    s.addShape("roundRect", { x: cx, y: s1Y + 0.25, w: c.w, h: cardH, rectRadius: 0.08, fill: { color: c.fill }, line: { color: c.border, width: 1.4 } });
    s.addText(c.title, { x: cx + 0.1, y: s1Y + 0.32, w: c.w - 0.2, h: 0.26, fontFace: FONT, fontSize: 12, bold: true, color: c.tc, align: "center", isTextBox: true, margin: 0 });
    vFlow(cx + 0.1, s1Y + 0.62, c.w - 0.2, cardH - 0.68, c.steps, c.tc, 10);
    s.addText(c.note, { x: cx + 0.1, y: s1Y + s1H - 0.32, w: c.w - 0.2, h: 0.3, fontFace: FONT, fontSize: 8, italic: true, color: bGRAY, align: "center", isTextBox: true, margin: 0, lineSpacing: 9.5 });
    cx += c.w;
    s.addText("→", { x: cx, y: s1Y + 0.25, w: gapArrow, h: cardH, fontFace: FONT, fontSize: 22, bold: true, color: bBLUE, align: "center", valign: "middle", isTextBox: true, margin: 0 });
    cx += gapArrow;
  });

  // Card 4 — TF-A (green, wider, with BL32/BL33 branch)
  const tfaW = 3.0;
  s.addShape("roundRect", { x: cx, y: s1Y + 0.25, w: tfaW, h: cardH, rectRadius: 0.08, fill: { color: bLGREEN }, line: { color: bGREEN, width: 1.6 } });
  s.addText("TRUSTED FIRMWARE-A", { x: cx + 0.1, y: s1Y + 0.32, w: tfaW - 0.2, h: 0.24, fontFace: FONT, fontSize: 11.5, bold: true, color: bGREEN, align: "center", isTextBox: true, margin: 0 });
  vFlow(cx + 0.7, s1Y + 0.6, tfaW - 1.4, 0.5, ["BL1", "BL2", "BL31"], bNAVY, 8);
  s.addText("EL3 Secure Monitor", { x: cx + 0.1, y: s1Y + 1.12, w: tfaW - 0.2, h: 0.16, fontFace: FONT, fontSize: 7.5, italic: true, color: bGRAY, align: "center", isTextBox: true, margin: 0 });
  s.addText("↓", { x: cx + tfaW / 2 - 0.9, y: s1Y + 1.3, w: 0.5, h: 0.14, fontFace: FONT, fontSize: 12, bold: true, color: bRED, align: "center", isTextBox: true, margin: 0 });
  s.addText("↓", { x: cx + tfaW / 2 + 0.4, y: s1Y + 1.3, w: 0.5, h: 0.14, fontFace: FONT, fontSize: 12, bold: true, color: bAMBER, align: "center", isTextBox: true, margin: 0 });
  const branchY = s1Y + 1.46, branchH = (s1Y + 0.25 + cardH) - branchY - 0.06;
  s.addShape("roundRect", { x: cx + 0.15, y: branchY, w: (tfaW - 0.4) / 2, h: branchH, rectRadius: 0.06, fill: { color: bLRED }, line: { color: bRED, width: 1.3 } });
  s.addText("BL32\nOP-TEE OS\nSecure World", { x: cx + 0.15, y: branchY, w: (tfaW - 0.4) / 2, h: branchH, fontFace: FONT, fontSize: 8, bold: true, color: "8A1F1B", align: "center", valign: "middle", isTextBox: true, margin: 0, lineSpacing: 9.5 });
  s.addShape("roundRect", { x: cx + tfaW - 0.15 - (tfaW - 0.4) / 2, y: branchY, w: (tfaW - 0.4) / 2, h: branchH, rectRadius: 0.06, fill: { color: bLAMBER }, line: { color: bAMBER, width: 1.3 } });
  s.addText("BL33\nU-Boot\nNon-Secure", { x: cx + tfaW - 0.15 - (tfaW - 0.4) / 2, y: branchY, w: (tfaW - 0.4) / 2, h: branchH, fontFace: FONT, fontSize: 8, bold: true, color: "7A5A00", align: "center", valign: "middle", isTextBox: true, margin: 0, lineSpacing: 9.5 });
  cx += tfaW;
  s.addText("→", { x: cx, y: s1Y + 0.25, w: gapArrow, h: cardH, fontFace: FONT, fontSize: 22, bold: true, color: bAMBER, align: "center", valign: "middle", isTextBox: true, margin: 0 });
  cx += gapArrow;

  // Card 5 — U-Boot (amber)
  const ubW = 12.9 - cx;
  s.addShape("roundRect", { x: cx, y: s1Y + 0.25, w: ubW, h: cardH, rectRadius: 0.08, fill: { color: bLAMBER }, line: { color: bAMBER, width: 1.4 } });
  s.addText("U-BOOT", { x: cx + 0.1, y: s1Y + 0.32, w: ubW - 0.2, h: 0.24, fontFace: FONT, fontSize: 12, bold: true, color: "7A5A00", align: "center", isTextBox: true, margin: 0 });
  vFlow(cx + 0.1, s1Y + 0.58, ubW - 0.2, cardH - 0.94, ["bootcmd", "Load DTB +\nkernel8.img", "Set bootargs → booti"], "7A5A00", 9.5);
  s.addText("Non-Secure / Normal World", { x: cx + 0.1, y: s1Y + s1H - 0.32, w: ubW - 0.2, h: 0.3, fontFace: FONT, fontSize: 8, italic: true, color: bGRAY, align: "center", isTextBox: true, margin: 0 });

  // ================= SECTION 2 — CPU1 SECURE-WORLD DIVERSION =================
  const s2Y = s1Y + s1H + 0.08, s2H = 2.64;
  s.addShape("roundRect", { x: 0.4, y: s2Y, w: 12.5, h: s2H, rectRadius: 0.08, fill: { color: bLBLUE }, line: { color: bBLUE, width: 1.6 } });
  s.addText("LINUX KERNEL BOOT", { x: 0.6, y: s2Y + 0.08, w: 5, h: 0.24, fontFace: FONT, fontSize: 14, bold: true, color: bBLUE, isTextBox: true, margin: 0 });
  s.addText("LINUX SMP BRING-UP", { x: 5.6, y: s2Y + 0.1, w: 3, h: 0.2, fontFace: FONT, fontSize: 9.5, bold: true, color: bGRAY, isTextBox: true, margin: 0, charSpacing: 1 });

  const cpuRowY = s2Y + 0.36, cpuW = 1.5, cpuH = 0.4, cpuGap = 0.25;
  const cpuLabels = [
    { t: "CPU0\nLinux", red: false }, { t: "CPU1\nSecure World", red: true },
    { t: "CPU2\nLinux", red: false }, { t: "CPU3\nLinux", red: false },
  ];
  let cpx = 0.6;
  cpuLabels.forEach((c) => {
    s.addShape("roundRect", { x: cpx, y: cpuRowY, w: cpuW, h: cpuH, rectRadius: 0.06, fill: { color: c.red ? bLRED : "FFFFFF" }, line: { color: c.red ? bRED : bBLUE, width: c.red ? 1.8 : 1.2 } });
    s.addText(c.t, { x: cpx, y: cpuRowY, w: cpuW, h: cpuH, fontFace: FONT, fontSize: 10, bold: true, color: c.red ? bRED : bNAVY, align: "center", valign: "middle", isTextBox: true, margin: 0, lineSpacing: 11 });
    cpx += cpuW + cpuGap;
  });

  // CPU1 diversion path — ONE single vertical red flow, each step its own box
  const pathY = cpuRowY + cpuH + 0.12;
  const pathW = 7.6, calloutX = 0.6 + pathW + 0.25, calloutW = 12.5 - 0.2 - (calloutX - 0.4);
  const stepH = 0.26, stepGapV = 0.09;
  const pathSteps = [
    "CPU1 online request",
    "Intercepted in EL3",
    "opteed_enter_secure_server_if_core1()",
    "CPU1 → Secure World",
    "rpi3_can_server_loop()",
  ];
  const pathH = pathSteps.length * stepH + (pathSteps.length - 1) * stepGapV;
  let psy = pathY;
  pathSteps.forEach((t, i) => {
    s.addShape("roundRect", { x: 0.6, y: psy, w: pathW, h: stepH, rectRadius: 0.05, fill: { color: bLRED }, line: { color: bRED, width: 1.5 } });
    s.addText(t, { x: 0.68, y: psy, w: pathW - 0.16, h: stepH, fontFace: FONT, fontSize: 10, bold: true, color: "8A1F1B", align: "center", valign: "middle", isTextBox: true, margin: 0 });
    psy += stepH;
    if (i < pathSteps.length - 1) {
      s.addText("↓", { x: 0.6, y: psy, w: pathW, h: stepGapV, fontFace: FONT, fontSize: 11, bold: true, color: bRED, align: "center", valign: "middle", isTextBox: true, margin: 0 });
      psy += stepGapV;
    }
  });

  s.addShape("roundRect", { x: calloutX, y: pathY, w: calloutW, h: pathH, rectRadius: 0.07, fill: { color: bNAVY }, line: { type: "none" } });
  s.addText("CPU1\nPERMANENTLY RESERVED\nFOR SECURE WORLD", { x: calloutX + 0.1, y: pathY, w: calloutW - 0.2, h: pathH, fontFace: FONT, fontSize: 11, bold: true, color: "FFFFFF", align: "center", valign: "middle", isTextBox: true, margin: 0, lineSpacing: 13 });

  const captionY = s2Y + s2H + 0.04;
  s.addText(
    "Linux continues with CPU0, CPU2 and CPU3.   Linux reports CPU1 offline.",
    { x: 0.6, y: captionY, w: 12.1, h: 0.2, fontFace: FONT, fontSize: 9.5, italic: true, bold: true, color: bGRAY, align: "center", isTextBox: true, margin: 0 }
  );

  // ================= SECTION 3 — FINAL RUNTIME STATE =================
  const s3Y = captionY + 0.24, s3H = 0.92;
  const nwX = 0.4, nwW = 6.0, swX2 = nwX + nwW + 0.2, swW3 = 12.9 - swX2;

  s.addShape("roundRect", { x: nwX, y: s3Y, w: nwW, h: s3H, rectRadius: 0.08, fill: { color: bLBLUE }, line: { color: bBLUE, width: 1.4 } });
  s.addText("NORMAL WORLD — LINUX", { x: nwX + 0.15, y: s3Y + 0.08, w: nwW - 0.3, h: 0.24, fontFace: FONT, fontSize: 11, bold: true, color: bBLUE, isTextBox: true, margin: 0 });
  s.addText("CPU0, CPU2, CPU3  —  Linux Kernel + Userspace", { x: nwX + 0.15, y: s3Y + 0.36, w: nwW - 0.3, h: 0.24, fontFace: FONT, fontSize: 9.5, bold: true, color: bNAVY, isTextBox: true, margin: 0 });
  s.addText("Applications: spi_poc_test  ·  secure_can_demo  ·  tee-supplicant", { x: nwX + 0.15, y: s3Y + 0.62, w: nwW - 0.3, h: 0.22, fontFace: FONT, fontSize: 9, color: bGRAY, isTextBox: true, margin: 0 });

  s.addShape("roundRect", { x: swX2, y: s3Y, w: swW3, h: s3H, rectRadius: 0.08, fill: { color: bLRED }, line: { color: bRED, width: 1.4 } });
  s.addText("SECURE WORLD — OP-TEE", { x: swX2 + 0.15, y: s3Y + 0.08, w: swW3 - 0.3, h: 0.24, fontFace: FONT, fontSize: 11, bold: true, color: bRED, isTextBox: true, margin: 0 });
  s.addText("CPU1  —  rpi3_can_server_loop()", { x: swX2 + 0.15, y: s3Y + 0.36, w: swW3 - 0.3, h: 0.24, fontFace: FONT, fontSize: 9.5, bold: true, color: bNAVY, isTextBox: true, margin: 0 });
  s.addText("SECURE CAN PROCESSING", { x: swX2 + 0.15, y: s3Y + 0.62, w: swW3 - 0.3, h: 0.16, fontFace: FONT, fontSize: 9, bold: true, color: bRED, isTextBox: true, margin: 0, charSpacing: 1 });
  s.addText("•  CAN interrupt handling      •  SPI0 CAN access      •  Secure validation", { x: swX2 + 0.15, y: s3Y + 0.78, w: swW3 - 0.3, h: 0.14, fontFace: FONT, fontSize: 9, bold: true, color: "8A1F1B", isTextBox: true, margin: 0 });

  // ================= CAN INTERRUPT PATH =================
  const ciHeadY = s3Y + s3H + 0.08;
  s.addText("CAN INTERRUPT PATH", { x: 0.4, y: ciHeadY, w: 4, h: 0.18, fontFace: FONT, fontSize: 11, bold: true, color: bNAVY, isTextBox: true, margin: 0 });
  const ciY = ciHeadY + 0.16, ciH = 0.3;
  const ciSteps = [
    { t: "MCP2518FD\nCAN Controller (SPI0)", red: false },
    { t: "GPIO25 / INT", red: false },
    { t: "BCM2837\nInterrupt Routing", red: false },
    { t: "Secure FIQ", red: true },
    { t: "CPU1 / OP-TEE", red: true },
  ];
  const ciGap = 0.32, ciW = (12.5 - (ciSteps.length - 1) * ciGap) / ciSteps.length;
  ciSteps.forEach((c, i) => {
    const x = 0.4 + i * (ciW + ciGap);
    s.addShape("roundRect", { x, y: ciY, w: ciW, h: ciH, rectRadius: 0.06, fill: { color: c.red ? bLRED : "F0F1F4" }, line: { color: c.red ? bRED : bGRAY, width: 1.3 } });
    s.addText(c.t, { x: x + 0.05, y: ciY, w: ciW - 0.1, h: ciH, fontFace: FONT, fontSize: 9, bold: true, color: c.red ? "8A1F1B" : bNAVY, align: "center", valign: "middle", isTextBox: true, margin: 0, lineSpacing: 10.5 });
    if (i < ciSteps.length - 1) {
      s.addText("→", { x: x + ciW, y: ciY, w: ciGap, h: ciH, fontFace: FONT, fontSize: 15, bold: true, color: c.red ? bRED : bGRAY, align: "center", valign: "middle", isTextBox: true, margin: 0 });
    }
  });

  // ================= BOTTOM BANNER =================
  const fbY = ciY + ciH + 0.09, fbH = 0.3;
  s.addShape("roundRect", { x: 0.4, y: fbY, w: 12.5, h: fbH, rectRadius: 0.07, fill: { color: bNAVY }, line: { type: "none" } });
  s.addText(
    [
      { text: "FINAL RUNTIME STATE:   ", options: { bold: true, color: bAMBER } },
      { text: "CPU0 + CPU2 + CPU3 → Linux / Normal World.   CPU1 → OP-TEE / Secure World → Secure CAN Processing.", options: { bold: true, color: "FFFFFF" } },
    ],
    { x: 0.6, y: fbY, w: 12.1, h: fbH, fontFace: FONT, fontSize: 10.5, valign: "middle", isTextBox: true, margin: 0 }
  );
}

// ============================================================= SLIDE 6 — AFTER BOOT (reference image, placed as-is)
{
  const s = p.addSlide();
  s.background = { color: "FFFFFF" };
  s.addImage({ path: "/home/aslam/Minor_Project/midterm_deck_v2/refs/slide6_after_boot_v3.png", x: 0, y: 0, w: 13.333, h: 7.5 });
  s.addImage({ path: LOGO, x: 12.55, y: 0.28, w: 0.55, h: 0.55 });
}

// ============================================================= SLIDE 7 — PROGRESS (reference image, placed as-is)
{
  const s = p.addSlide();
  s.background = { color: "FFFFFF" };
  s.addImage({ path: "/home/aslam/Minor_Project/midterm_deck_v2/refs/slide7_progress_v6.png", x: 0, y: 0, w: 13.333, h: 7.5 });
  s.addImage({ path: LOGO, x: 12.55, y: 0.28, w: 0.55, h: 0.55 });
}

// ============================================================= SLIDE 8 — WHAT WE'LL DELIVER NEXT
{
  const s = p.addSlide();
  s.background = { color: "F7F8FA" };
  addHeader(s, "SUMMARY", "What I'll Deliver Next",
    "The concrete work planned between now and the final presentation.");
  addLogo(s);

  const nextSteps = [
    ["HMAC authentication", "BLOCKED", "B8560E", ORANGE_BG, "Unresolved Secure World runtime issue - resolve or clearly document the root cause"],
    ["Reduce validation latency", "NOT STARTED", "8A8F98", GREY_BG, "Measure and optimize latency / throughput / CPU-overhead of the validation pipeline"],
    ["Store validated packets in flash memory", "NOT STARTED", "8A8F98", GREY_BG, "Tamper-evident logging design; RPi3 has no RPMB, storage boundary still to be designed"],
    ["Rate & flood detection", "NOT STARTED", "8A8F98", GREY_BG, "Defend against a CAN-bus flood / denial-of-service"],
    ["End-to-end demo with real measurements", "NOT STARTED", "8A8F98", GREY_BG, "Full hardware demonstration with real captured data, not placeholder values"],
  ];

  const rowY0 = 1.9, rowH = 0.78, rowGap = 0.1;
  nextSteps.forEach((d, i) => {
    const y = rowY0 + i * (rowH + rowGap);
    s.addShape("roundRect", { x: 0.5, y, w: 12.3, h: rowH, rectRadius: 0.07, fill: { color: "FFFFFF" }, line: { color: "E3D9BE", width: 1.2 } });
    s.addText(d[0], { x: 0.72, y: y + 0.1, w: 11.3, h: 0.28, fontFace: FONT, fontSize: 12.5, bold: true, color: "22262E", isTextBox: true, margin: 0 });
    s.addText(d[4], { x: 0.72, y: y + 0.42, w: 11.8, h: 0.3, fontFace: FONT, fontSize: 10, color: "5B5237", isTextBox: true, margin: 0, lineSpacing: 12 });
  });

  const bannerY = rowY0 + nextSteps.length * (rowH + rowGap) + 0.1;
  s.addShape("roundRect", { x: 0.5, y: bannerY, w: 12.3, h: 0.55, rectRadius: 0.08, fill: { color: NAVY }, line: { type: "none" } });
  s.addText(
    [
      { text: "By the final presentation:  ", options: { bold: true, color: "F0C27B" } },
      { text: "these five items resolved, implemented, or clearly documented - nothing promised beyond this list.", options: { color: "F5F6F8" } },
    ],
    { x: 0.7, y: bannerY, w: 11.9, h: 0.55, fontFace: FONT, fontSize: 11, valign: "middle", isTextBox: true, margin: 0 }
  );

  addFooter(s, 8, TOTAL_SLIDES, false);
}

// ============================================================= SLIDE 9 — THANK YOU
{
  const s = p.addSlide();
  s.background = { color: "F2F3F5" };

  s.addText("Thank You", {
    x: 0.7, y: 3.4, w: 11.8, h: 1.1, fontFace: FONT, fontSize: 54, bold: true, color: NAVY, align: "center", isTextBox: true, margin: 0,
  });
  s.addImage({ path: LOGO, x: 11.9, y: 0.45, w: 0.85, h: 0.85 });
}

p.writeFile({ fileName: "/home/aslam/Minor_Project/Secure_CAN_Gateway_Midterm.pptx" }).then(() => {
  console.log("Saved: Secure_CAN_Gateway_Midterm.pptx");
});
