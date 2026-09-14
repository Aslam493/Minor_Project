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


def set_lines(shape, lines, *, size, font="Aptos", color="192632", bold=False,
              spacing=3, mono=False):
    frame = shape.text_frame
    frame.clear()
    for index, line in enumerate(lines):
        paragraph = frame.paragraphs[0] if index == 0 else frame.add_paragraph()
        paragraph.text = line
        paragraph.alignment = PP_ALIGN.LEFT
        paragraph.space_after = Pt(spacing)
        for run in paragraph.runs:
            run.font.name = "DejaVu Sans Mono" if mono else font
            run.font.size = Pt(size)
            run.font.bold = bold
            run.font.color.rgb = RGBColor.from_string(color)


prs = Presentation(PPT_PATH)
slide = prs.slides[4]

set_lines(
    shape_by_name(slide, "TextBox 9"),
    [
        "●  Configured a minimal ARM64 Buildroot system",
        "●  Generated the Linux root filesystem",
        "●  Integrated the OP-TEE Normal World runtime",
        "●  Added test clients, examples and signed TAs",
    ],
    size=14,
    bold=True,
    spacing=8,
)

set_lines(
    shape_by_name(slide, "TextBox 14"),
    [
        "out-br/",
        "├── images/rootfs.ext2",
        "└── target/",
        "    ├── bin/, sbin/, etc/",
        "    ├── usr/lib/libteec.so",
        "    ├── usr/sbin/tee-supplicant",
        "    ├── usr/bin/xtest",
        "    ├── usr/bin/optee_example_*",
        "    └── lib/optee_armtz/*.ta",
    ],
    size=11,
    color="AE4343",
    bold=True,
    spacing=1,
    mono=True,
)

set_lines(
    shape_by_name(slide, "TextBox 17"),
    ["Boot-ready ARM64 root filesystem with the OP-TEE user-space environment."],
    size=11.5,
    bold=True,
)

set_lines(
    shape_by_name(slide, "TextBox 19"),
    ["Created here; packaged into the SD image in Step 7 and verified in Steps 8–9."],
    size=10.5,
    color="5B6A74",
)

prs.save(PPT_PATH)
print(PPT_PATH)
