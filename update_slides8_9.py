from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN
from pptx.util import Pt


PPT_PATH = "/home/aslam/Minor_Project/Minor_Project_Steps_1_to_10_Updated.pptx"


def shape_by_name(slide, name):
    for shape in slide.shapes:
        if shape.name == name:
            return shape
    raise KeyError(f"Shape not found: {name}")


def set_lines(shape, lines, *, size, color="192632", bold=False,
              mono=False, spacing=2):
    frame = shape.text_frame
    frame.clear()
    for index, line in enumerate(lines):
        paragraph = frame.paragraphs[0] if index == 0 else frame.add_paragraph()
        paragraph.text = line
        paragraph.alignment = PP_ALIGN.LEFT
        paragraph.space_after = Pt(spacing)
        for run in paragraph.runs:
            run.font.name = "DejaVu Sans Mono" if mono else "Aptos"
            run.font.size = Pt(size)
            run.font.bold = bold
            run.font.color.rgb = RGBColor.from_string(color)


prs = Presentation(PPT_PATH)

# Step 8: physical boot and access evidence.
slide8 = prs.slides[7]
shape_by_name(slide8, "TextBox 46").text = "Boot the custom image on Raspberry Pi 3"
shape_by_name(slide8, "Rounded Rectangle 16").text = "Hardware boot proof"
shape_by_name(slide8, "TextBox 47").text = "WHAT I DID"
set_lines(
    shape_by_name(slide8, "TextBox 48"),
    [
        "●  Flashed rpi3-sdcard.img to the 8 GB test card",
        "●  Booted Raspberry Pi 3 from the custom SD image",
        "●  Reached the Buildroot root shell over HDMI",
        "●  Confirmed AArch64 Linux and four CPU cores",
        "●  Obtained an Ethernet address and connected by SSH",
    ],
    size=12.5,
    bold=True,
    spacing=5,
)
shape_by_name(slide8, "TextBox 49").text = "BOOT EVIDENCE"
shape_by_name(slide8, "TextBox 50").text = "observed on the real board"
set_lines(
    shape_by_name(slide8, "TextBox 51"),
    [
        "rpi3-sdcard.img",
        "       ↓  dd to /dev/sda",
        "8 GB SD card → Raspberry Pi 3",
        "       ↓",
        "Firmware → Secure boot chain → Linux",
        "       ↓",
        "Buildroot login and root shell",
        "       ↓",
        "eth0 DHCP → SSH access",
        "",
        "$ whoami       → root",
        "$ uname -m     → aarch64",
        "$ ip -4 addr   → 10.237.27.131/23",
    ],
    size=10.2,
    color="AE4343",
    bold=True,
    mono=True,
    spacing=0,
)
set_lines(
    shape_by_name(slide8, "TextBox 53"),
    ["The custom image booted on real hardware and was accessible locally and remotely."],
    size=10.8,
    bold=True,
)
set_lines(
    shape_by_name(slide8, "TextBox 55"),
    ["HDMI/SSH boot proved; an early-stage UART capture still requires a USB-to-TTL adapter."],
    size=9.8,
    color="5B6A74",
)

# Step 9: OP-TEE driver, device, supplicant and regression evidence.
slide9 = prs.slides[8]
shape_by_name(slide9, "TextBox 32").text = "Prove Linux-to-OP-TEE communication"
shape_by_name(slide9, "Rounded Rectangle 8").text = "Secure runtime proof"
shape_by_name(slide9, "TextBox 33").text = "VERIFICATION RESULTS"
set_lines(
    shape_by_name(slide9, "TextBox 34"),
    [
        "●  Linux detected OP-TEE revision 4.10",
        "●  OP-TEE kernel driver initialized successfully",
        "●  /dev/tee0 and /dev/teepriv0 were created",
        "●  tee-supplicant started automatically",
        "●  xtest completed with zero failures",
    ],
    size=12.5,
    bold=True,
    spacing=5,
)
shape_by_name(slide9, "TextBox 35").text = "COMMUNICATION + PROOF"
shape_by_name(slide9, "TextBox 36").text = "Normal World to Secure World"
set_lines(
    shape_by_name(slide9, "TextBox 37"),
    [
        "Normal World",
        "xtest → libteec → /dev/tee0",
        "           ↓ Linux OP-TEE driver",
        "           ↓ SMC via TF-A BL31",
        "Secure World",
        "OP-TEE BL32 → signed Trusted Application",
        "",
        "$ dmesg | grep -i optee",
        "  revision 4.10; initialized driver",
        "$ ls /dev/tee*",
        "  /dev/tee0  /dev/teepriv0",
        "$ xtest",
        "  114 cases | 36,114 subtests",
        "  0 failed   | 0 skipped",
    ],
    size=9.5,
    color="AE4343",
    bold=True,
    mono=True,
    spacing=0,
)
set_lines(
    shape_by_name(slide9, "TextBox 39"),
    ["Linux successfully invoked OP-TEE services and executed signed TAs in Secure World."],
    size=10.8,
    bold=True,
)
set_lines(
    shape_by_name(slide9, "TextBox 41"),
    ["The generic OP-TEE platform passed; the project-specific secure application follows."],
    size=10,
    color="5B6A74",
)

prs.save(PPT_PATH)
print(PPT_PATH)
