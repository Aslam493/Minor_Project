from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN, MSO_ANCHOR
from pptx.enum.shapes import MSO_SHAPE

OUT = "/home/aslam/Minor_Project/Minor_Project_5_Phase_Implementation.pptx"
prs = Presentation()
prs.slide_width = Inches(13.333)
prs.slide_height = Inches(7.5)

NAVY = RGBColor(20, 52, 81)
TEAL = RGBColor(20, 112, 127)
BLUE = RGBColor(229, 243, 247)
PALE = RGBColor(244, 248, 250)
GREEN = RGBColor(225, 244, 230)
AMBER = RGBColor(255, 244, 211)
RED = RGBColor(174, 55, 55)
WHITE = RGBColor(255, 255, 255)
DARK = RGBColor(31, 43, 54)
GRAY = RGBColor(86, 103, 113)

def rect(slide, x, y, w, h, fill, line=TEAL, radius=False):
    shp = slide.shapes.add_shape(MSO_SHAPE.ROUNDED_RECTANGLE if radius else MSO_SHAPE.RECTANGLE, Inches(x), Inches(y), Inches(w), Inches(h))
    shp.fill.solid(); shp.fill.fore_color.rgb = fill
    shp.line.color.rgb = line
    return shp

def textbox(slide, text, x, y, w, h, size=16, color=DARK, bold=False, font="Aptos", align=PP_ALIGN.LEFT, margin=.08, valign=MSO_ANCHOR.TOP):
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame; tf.clear(); tf.word_wrap = True
    tf.margin_left = tf.margin_right = Inches(margin); tf.margin_top = tf.margin_bottom = Inches(margin)
    tf.vertical_anchor = valign
    p = tf.paragraphs[0]; p.text = text; p.alignment = align
    p.font.name = font; p.font.size = Pt(size); p.font.bold = bold; p.font.color.rgb = color
    return box

def rich_box(slide, title, lines, x, y, w, h, fill=PALE, title_color=NAVY, code=False):
    shp = rect(slide, x, y, w, h, fill, TEAL, True)
    tf = shp.text_frame; tf.clear(); tf.word_wrap = True
    tf.margin_left = tf.margin_right = Inches(.15); tf.margin_top = tf.margin_bottom = Inches(.1)
    p = tf.paragraphs[0]; p.text = title; p.font.name = "Aptos Display"; p.font.size = Pt(16); p.font.bold = True; p.font.color.rgb = title_color
    for line in lines:
        p = tf.add_paragraph(); p.text = line; p.font.name = "Aptos Mono" if code else "Aptos"; p.font.size = Pt(10.5 if code else 12); p.font.color.rgb = DARK; p.space_after = Pt(1)
    return shp

def arrow(slide, x, y, label):
    shp = slide.shapes.add_shape(MSO_SHAPE.CHEVRON, Inches(x), Inches(y), Inches(1.25), Inches(.75))
    shp.fill.solid(); shp.fill.fore_color.rgb = TEAL; shp.line.color.rgb = TEAL
    tf = shp.text_frame; tf.clear(); tf.vertical_anchor = MSO_ANCHOR.MIDDLE
    p = tf.paragraphs[0]; p.text = label; p.alignment = PP_ALIGN.CENTER; p.font.name = "Aptos"; p.font.size = Pt(8); p.font.bold = True; p.font.color.rgb = WHITE

def header(slide, phase, title, status, status_color):
    rect(slide, 0, 0, 13.333, .72, NAVY, NAVY)
    textbox(slide, phase, .35, .11, 1.45, .43, 14, WHITE, True)
    textbox(slide, title, 1.65, .08, 8.9, .5, 24, WHITE, True, "Aptos Display")
    tag = rect(slide, 10.85, .13, 2.1, .38, status_color, status_color, True)
    tf = tag.text_frame; tf.clear(); tf.vertical_anchor = MSO_ANCHOR.MIDDLE
    p = tf.paragraphs[0]; p.text = status; p.alignment = PP_ALIGN.CENTER; p.font.name = "Aptos"; p.font.size = Pt(12); p.font.bold = True; p.font.color.rgb = DARK

def footer(slide, purpose, proof, page, warning=False):
    fill = AMBER if warning else BLUE
    rect(slide, .35, 6.18, 12.63, .91, fill, RGBColor(205, 221, 227), True)
    textbox(slide, "WHY INCLUDED", .55, 6.31, 1.55, .22, 10, TEAL, True)
    textbox(slide, purpose, 2.0, 6.24, 5.3, .55, 12, DARK, False)
    textbox(slide, "WHAT THIS PROVES", 7.45, 6.31, 1.75, .22, 10, TEAL if not warning else RED, True)
    textbox(slide, proof, 9.05, 6.23, 3.48, .58, 11.5, DARK, False)
    textbox(slide, str(page), 12.66, 7.15, .25, .2, 8, GRAY, False, align=PP_ALIGN.RIGHT)

def phase_slide(phase, title, before, changes, after, purpose, proof, page, status="COMPLETE", status_fill=GREEN, warning=False, qa=None):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    slide.background.fill.solid(); slide.background.fill.fore_color.rgb = WHITE
    header(slide, phase, title, status, status_fill)
    rich_box(slide, "BEFORE — source tree", before, .35, 1.0, 3.45, 4.72, PALE, code=True)
    arrow(slide, 3.98, 2.25, "")
    rich_box(slide, "CHANGE MADE", changes, 5.23, 1.0, 3.0, 2.55, BLUE, code=False)
    if qa:
        rich_box(slide, "VIVA: WHY?", qa, 5.23, 3.72, 3.0, 2.0, AMBER, code=False)
    arrow(slide, 8.4, 2.25, "")
    rich_box(slide, "AFTER — generated tree", after, 9.63, 1.0, 3.35, 4.72, PALE, code=True)
    footer(slide, purpose, proof, page, warning)
    return slide

phase_slide(
    "PHASE 1", "Secure World foundation — OP-TEE OS",
    ["optee_os/", "├─ core/      secure-kernel source", "├─ lib/       secure APIs", "├─ mk/        build rules", "└─ ta/        in-tree TA source"],
    ["• Selected Raspberry Pi 3 platform", "• Selected AArch64 Secure World", "• Cross-compiled on x86 laptop", "• Generated TF-A-compatible split image"],
    ["optee_os/out/arm/  NEW", "├─ core/tee.bin", "├─ core/tee.elf", "├─ core/tee-header_v2.bin", "├─ core/tee-pager_v2.bin", "└─ export-ta_arm64/"],
    "OP-TEE provides the protected environment in which Trusted Applications and future CAN validation logic execute.",
    "Secure World code compiled; physical boot is not yet proven.", 1,
    qa=["Why not Linux?", "Linux is Normal World and cannot be trusted with final secret-key decisions."])

phase_slide(
    "PHASE 2", "Secure boot packaging — TF-A + U-Boot",
    ["u-boot/{arch, board, cmd, configs, drivers}", "", "trusted-firmware-a/", "├─ bl1/", "├─ plat/rpi3/", "└─ services/"],
    ["• Built U-Boot as BL33", "• Used OP-TEE as BL32", "• Built TF-A secure monitor", "• Packaged BL32 + BL33 into FIP", "• Appended FIP after padded BL1"],
    ["u-boot/", "└─ u-boot.bin  NEW", "", "TF-A/build/rpi3/debug/", "├─ fip.bin", "└─ armstub8.bin", "   = padded BL1 + FIP"],
    "TF-A initializes security and switches worlds; U-Boot loads Linux, the DTB and boot arguments.",
    "BL32/BL33 compiled and packaged; board boot is not yet proven.", 2,
    qa=["Why both?", "TF-A = secure monitor.", "U-Boot = flexible Linux loader."])

phase_slide(
    "PHASE 3", "Normal World kernel + existing hardware description",
    ["linux/", "├─ arch/arm64/", "├─ arch/arm64/boot/dts/", "├─ drivers/", "├─ fs/ and net/", "└─ Makefile"],
    ["• Configured ARM64/RPi3 kernel", "• Enabled CONFIG_TEE=y", "• Enabled CONFIG_OPTEE=y", "• Compiled Linux source", "• Compiled existing RPi3 DTS files", "• No new hardware was added"],
    ["linux/", "├─ .config", "├─ arch/arm64/boot/Image", "├─ .../bcm2710-rpi-3-b.dtb", "├─ .../rpi-3-b-plus.dtb", "└─ generated .o / .ko", "", "Copied → Step3_Output/"],
    "Linux runs Normal World; the driver gives Linux the capability to call OP-TEE. DTBs describe existing RPi3 hardware.",
    "Kernel/DTBs compiled with OP-TEE support; communication is not proven.", 3,
    qa=["Why DTB again?", "We compiled existing DTS from the controlled source; we did not customize hardware."])

phase_slide(
    "PHASE 4", "Reproducibility gate — verify, compare and preserve",
    ["Minor_Project/", "├─ optee_full/        complete", "├─ optee_project/     incomplete", "├─ Step3_Output/", "└─ loose build outputs", "", "Pi: Minor_Project/optee_full = empty"],
    ["• Selected optee_full as authority", "• Compared laptop/Pi Git revisions", "• Verified required repositories", "• Backed up Step 1–3 outputs", "• Recorded SHA-256 checksums", "• Read official rpi3.mk targets"],
    ["Minor_Project/", "├─ optee_full/  SELECTED", "├─ Step3_Output/", "└─ Step4_Verification/", "   ├─ STEP4_COMPLETION.md", "   └─ verified_artifacts/", "      ├─ optee/  ├─ tf-a/", "      └─ u-boot/ └─ linux/"],
    "Prevents mixed revisions, empty trees, lost binaries, corrupted files and unsupported packaging commands.",
    "One controlled, checksummed and reproducible build base was established.", 4,
    qa=["New runtime feature?", "No. This phase is quality control and evidence preservation."])

phase_slide(
    "PHASE 5", "Linux user space — Buildroot and OP-TEE runtime",
    ["optee_full/", "├─ buildroot/       package recipes", "├─ optee_client/    source", "├─ optee_test/      source", "├─ optee_examples/  source", "└─ out-br/          incomplete/absent"],
    ["• Started official buildroot target", "• Continued kernel/module build", "• Generated ~1,713 module files", "• Entered Buildroot package phase", "• Build stopped: compiler prefix/path mismatch"],
    ["EXPECTED out-br/", "├─ target/usr/sbin/tee-supplicant", "├─ target/usr/lib/libteec.so", "├─ target/usr/bin/xtest", "├─ target/usr/bin/optee_example_*", "├─ target/lib/optee_armtz/*.ta", "├─ target/lib/modules/*.ko", "└─ images/rootfs.ext4", "", "CURRENT: final files not generated"],
    "The kernel alone has no shell, libraries or OP-TEE clients. Buildroot creates the usable ARM64 root filesystem.",
    "Partially built; fix AArch64 toolchain mismatch, resume, then verify all files.", 5,
    status="IN PROGRESS", status_fill=AMBER, warning=True,
    qa=["Runtime roles", "libteec = client API", "tee-supplicant = RPC help", "xtest = end-to-end test", ".ta = secure application"])

prs.save(OUT)
print(OUT)
