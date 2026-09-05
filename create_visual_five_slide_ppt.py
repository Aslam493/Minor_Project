from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN, MSO_ANCHOR
from pptx.enum.shapes import MSO_SHAPE

OUT="/home/aslam/Minor_Project/Minor_Project_5_Phase_Implementation.pptx"
r=Presentation(); r.slide_width=Inches(13.333); r.slide_height=Inches(7.5)
INK=RGBColor(25,38,50); MUTED=RGBColor(91,106,116); WHITE=RGBColor(255,255,255)
BG=RGBColor(247,249,250); CARD=RGBColor(255,255,255); LINE=RGBColor(218,226,230)
COLORS=[RGBColor(41,105,150),RGBColor(69,83,154),RGBColor(27,133,119),RGBColor(189,112,38),RGBColor(174,67,67)]
PALES=[RGBColor(229,240,248),RGBColor(234,235,248),RGBColor(228,244,240),RGBColor(251,240,225),RGBColor(249,232,232)]

def box(slide,x,y,w,h,fill,line=LINE,round=True):
    s=slide.shapes.add_shape(MSO_SHAPE.ROUNDED_RECTANGLE if round else MSO_SHAPE.RECTANGLE,Inches(x),Inches(y),Inches(w),Inches(h))
    s.fill.solid(); s.fill.fore_color.rgb=fill; s.line.color.rgb=line; return s

def txt(slide,v,x,y,w,h,size=15,color=INK,bold=False,font="Aptos",align=PP_ALIGN.LEFT):
    b=slide.shapes.add_textbox(Inches(x),Inches(y),Inches(w),Inches(h)); t=b.text_frame; t.clear(); t.word_wrap=True
    t.margin_left=t.margin_right=Inches(.03); t.margin_top=t.margin_bottom=Inches(.02)
    p=t.paragraphs[0]; p.text=v; p.alignment=align; p.font.name=font; p.font.size=Pt(size); p.font.bold=bold; p.font.color.rgb=color
    return b

def bullet_list(slide,items,x,y,w,h,color):
    b=slide.shapes.add_textbox(Inches(x),Inches(y),Inches(w),Inches(h)); t=b.text_frame; t.clear(); t.word_wrap=True
    t.margin_left=Inches(.02); t.margin_right=Inches(.03); t.margin_top=Inches(.02)
    for i,(head,detail) in enumerate(items):
        p=t.paragraphs[0] if i==0 else t.add_paragraph(); p.text=""; p.space_after=Pt(12)
        run=p.add_run(); run.text="●  "; run.font.name="Aptos"; run.font.size=Pt(13); run.font.color.rgb=color
        run=p.add_run(); run.text=head; run.font.name="Aptos"; run.font.size=Pt(15); run.font.bold=True; run.font.color.rgb=INK
        run=p.add_run(); run.text="\n    "+detail; run.font.name="Aptos"; run.font.size=Pt(11.5); run.font.color.rgb=MUTED
    return b

def tree(slide,lines,x,y,w,h,accent):
    b=slide.shapes.add_textbox(Inches(x),Inches(y),Inches(w),Inches(h)); t=b.text_frame; t.clear(); t.word_wrap=False
    t.margin_left=Inches(.03); t.margin_top=Inches(.02)
    for i,line in enumerate(lines):
        p=t.paragraphs[0] if i==0 else t.add_paragraph(); p.text=line; p.font.name="DejaVu Sans Mono"; p.font.size=Pt(12); p.font.color.rgb=accent if i==0 or line.isupper() else INK; p.font.bold=(i==0 or line.isupper()); p.space_after=Pt(2)
    return b

def make_slide(n,title,goal,changes,tree_lines,outcome,boundary,status="COMPLETE"):
    c=COLORS[n-1]; pale=PALES[n-1]
    s=r.slides.add_slide(r.slide_layouts[6]); s.background.fill.solid(); s.background.fill.fore_color.rgb=BG
    box(s,0,0,1.25,7.5,c,c,False)
    txt(s,"PHASE",.18,.45,.88,.3,12,WHITE,True,align=PP_ALIGN.CENTER)
    txt(s,str(n),.12,.83,1.0,1.0,44,WHITE,True,"Aptos Display",PP_ALIGN.CENTER)
    txt(s,status,.14,6.65,.96,.3,9,WHITE,True,align=PP_ALIGN.CENTER)
    txt(s,title,1.65,.46,8.9,.48,25,INK,True,"Aptos Display")
    tag=box(s,10.8,.48,2.05,.38,pale,pale)
    tf=tag.text_frame; tf.clear(); tf.vertical_anchor=MSO_ANCHOR.MIDDLE
    p=tf.paragraphs[0]; p.text=goal; p.alignment=PP_ALIGN.CENTER; p.font.name="Aptos"; p.font.size=Pt(10); p.font.bold=True; p.font.color.rgb=c

    box(s,1.65,1.3,4.25,4.72,CARD)
    txt(s,"WHAT CHANGED",1.92,1.58,3.7,.3,13,c,True,"Aptos Display")
    bullet_list(s,changes,1.92,2.05,3.68,3.72,c)

    box(s,6.15,1.3,6.7,4.72,CARD)
    txt(s,"OUTPUT TREE",6.45,1.58,2.4,.3,13,c,True,"Aptos Display")
    txt(s,"generated / verified files",9.65,1.61,2.85,.25,9,MUTED,False,align=PP_ALIGN.RIGHT)
    box(s,6.43,2.0,6.13,3.72,pale,pale)
    tree(s,tree_lines,6.72,2.24,5.6,3.22,c)

    box(s,1.65,6.25,11.2,.72,pale,pale)
    txt(s,"OUTCOME",1.92,6.47,.85,.22,10,c,True)
    txt(s,outcome,2.75,6.38,5.0,.38,12,INK,True)
    txt(s,"BOUNDARY",7.85,6.47,1.0,.22,10,c,True)
    txt(s,boundary,8.78,6.38,3.72,.4,11.5,MUTED)
    txt(s,"ARM TrustZone + OP-TEE on Raspberry Pi 3",1.65,7.2,5,.18,8,MUTED)

make_slide(1,"Build the Secure World","OP-TEE OS",
    [("Target selected","Raspberry Pi 3, AArch64"),("Secure OS compiled","Cross-built on the laptop"),("Boot payload prepared","TF-A-compatible split images")],
    ["optee_os/","`-- out/arm/core/","    |-- tee.bin          [Secure World]","    |-- tee.elf          [debug image]","    |-- tee-header_v2.bin [TF-A header]","    |-- tee-pager_v2.bin  [pager payload]","    `-- tee-pageable_v2.bin","","Loaded by TF-A as BL32"],
    "Secure World binaries are available.","Compilation only; not booted.")

make_slide(2,"Package the secure boot chain","TF-A + U-Boot",
    [("U-Boot built","Normal World payload: BL33"),("TF-A configured","OP-TEE dispatcher + BL32"),("Boot image packaged","BL1 and FIP combined")],
    ["u-boot/","|-- u-boot.bin       [BL33]","|-- u-boot             [ELF/debug]","`-- System.map        [symbols]","","trusted-firmware-a/","`-- build/rpi3/debug/","    |-- bl1.bin         [early stage]","    |-- fip.bin         [BL32 + BL33]","    `-- armstub8.bin    [BL1 + FIP]"],
    "Secure and Normal payloads are packaged.","Packaging only; not booted.")

make_slide(3,"Prepare the Normal World kernel","Linux + standard DTBs",
    [("Kernel configured","CONFIG_TEE=y, CONFIG_OPTEE=y"),("ARM64 Linux compiled","Raspberry Pi 3 target"),("Existing DTS compiled","No new hardware or custom node")],
    ["Step3_Output/","|-- Image                 [ARM64 kernel]","|-- linux.config          [kernel config]","`-- dtbs/","    |-- bcm2710-rpi-3-b.dtb       [Model B]","    `-- bcm2710-rpi-3-b-plus.dtb  [Model B+]","","Loaded by U-Boot with Image"],
    "Kernel and board descriptions are verified.","Communication not yet proven.")

make_slide(4,"Create a reproducible build base","Verify + preserve",
    [("Authoritative tree selected","Laptop optee_full"),("Versions compared","Laptop and Pi revisions match"),("Evidence preserved","Backups + SHA-256 checksums")],
    ["Step4_Verification/","|-- STEP4_COMPLETION.md","`-- verified_artifacts/","    |-- optee/","    |   `-- tee*.bin + tee.elf","    |-- tf-a/","    |   `-- armstub8.bin + fip.bin","    |-- u-boot/","    |   `-- u-boot.bin","    `-- linux/","        `-- Image + DTBs + config"],
    "One controlled, checksummed workspace.","Quality control; no runtime feature.")

make_slide(5,"Build the Linux user space","Buildroot runtime",
    [("Buildroot started","Official RPi3 target, four jobs"),("Modules generated","About 1,713 module-related files"),("Build stopped","AArch64 compiler prefix mismatch")],
    ["CURRENT (partial)","optee_full/","|-- module_output/  (~1,713 files)","`-- out-br/","    |-- build/","    |-- host/","    `-- target/ (incomplete)","","EXPECTED AFTER SUCCESS","out-br/","|-- target/usr/sbin/tee-supplicant","|-- target/usr/lib/libteec.so","|-- target/usr/bin/xtest + examples","|-- target/lib/optee_armtz/*.ta","`-- images/rootfs.ext4"],
    "Kernel modules and partial Buildroot tree exist.","Fix toolchain; rootfs not generated.","IN PROGRESS")

r.save(OUT); print(OUT)
