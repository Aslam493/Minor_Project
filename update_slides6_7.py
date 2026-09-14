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
              mono=False, spacing=1):
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

# Step 6: assemble and connect the files that participate in boot.
slide6 = prs.slides[5]
shape_by_name(slide6, "Rounded Rectangle 26").text = "Boot-chain assembly"
set_lines(
    shape_by_name(slide6, "TextBox 76"),
    [
        "●  Collected all Raspberry Pi boot files",
        "●  Built armstub8.bin from TF-A BL1 + FIP",
        "●  FIP contains BL2, BL31, OP-TEE BL32 and U-Boot BL33",
        "●  Added Linux, DTBs and the U-Boot environment",
        "●  Defined the handoff from firmware to Linux",
    ],
    size=13,
    bold=True,
    spacing=5,
)
set_lines(
    shape_by_name(slide6, "TextBox 79"),
    [
        "optee_full/out/boot/",
        "├── bootcode.bin + start*.elf + fixup*.dat",
        "│   └── Raspberry Pi GPU firmware",
        "├── config.txt              [boot selection]",
        "├── armstub8.bin",
        "│   ├── TF-A BL1",
        "│   └── FIP",
        "│       ├── TF-A BL2",
        "│       ├── TF-A BL31       [EL3 monitor]",
        "│       ├── OP-TEE BL32      [Secure World]",
        "│       └── U-Boot BL33      [Normal World]",
        "├── kernel8.img             [Linux kernel]",
        "├── bcm2710-*.dtb           [hardware data]",
        "└── uboot.env               [kernel handoff]",
        "",
        "BOOT: Firmware → BL1 → BL2 → BL31 → BL32",
        "      → BL33/U-Boot → Linux",
    ],
    size=9.2,
    color="AE4343",
    bold=True,
    mono=True,
    spacing=0,
)
set_lines(
    shape_by_name(slide6, "TextBox 81"),
    ["One connected boot payload with a defined Secure-to-Normal World handoff."],
    size=11,
    bold=True,
)
set_lines(
    shape_by_name(slide6, "TextBox 83"),
    ["Files assembled here; the boot and rootfs partitions are packaged in Step 7."],
    size=10.5,
    color="5B6A74",
)

# Step 7: package the Step 6 boot payload and Step 5 rootfs into one disk image.
slide7 = prs.slides[6]
shape_by_name(slide7, "Rounded Rectangle 21").text = "Two-partition image"
set_lines(
    shape_by_name(slide7, "TextBox 62"),
    [
        "●  Created one MBR SD-card image",
        "●  Partition 1 received the Step 6 boot payload",
        "●  Partition 2 received the Step 5 Buildroot rootfs",
        "●  Flashed and compared the 8 GB test card",
        "●  Confirmed the written files matched the image",
    ],
    size=13,
    bold=True,
    spacing=5,
)
set_lines(
    shape_by_name(slide7, "TextBox 65"),
    [
        "rpi3-sdcard.img                 [MBR disk]",
        "├── Partition 1: FAT16 / boot    [~63 MiB]",
        "│   ├── Raspberry Pi firmware",
        "│   ├── config.txt",
        "│   ├── armstub8.bin",
        "│   │   └── BL1 + FIP(BL2/BL31/BL32/BL33)",
        "│   ├── kernel8.img + DTBs",
        "│   └── uboot.env",
        "└── Partition 2: ext4 / rootfs  [~256 MiB]",
        "    ├── bin/, sbin/, etc/        [Buildroot]",
        "    ├── lib/modules/             [Linux modules]",
        "    ├── usr/lib/libteec.so",
        "    ├── usr/sbin/tee-supplicant",
        "    ├── usr/bin/xtest + examples",
        "    └── lib/optee_armtz/*.ta     [signed TAs]",
        "",
        "Image write verification: MATCH",
    ],
    size=9.2,
    color="AE4343",
    bold=True,
    mono=True,
    spacing=0,
)
set_lines(
    shape_by_name(slide7, "TextBox 67"),
    ["One flashable image containing both the boot chain and Linux root filesystem."],
    size=11,
    bold=True,
)
set_lines(
    shape_by_name(slide7, "TextBox 69"),
    ["Packaging completed here; physical boot and runtime proof follow in Steps 8–9."],
    size=10.5,
    color="5B6A74",
)

prs.save(PPT_PATH)
print(PPT_PATH)
