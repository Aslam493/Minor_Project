from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN, MSO_ANCHOR
from pptx.enum.shapes import MSO_SHAPE

OUT = "/home/aslam/Minor_Project/Minor_Project_5_Phase_Implementation.pptx"
prs = Presentation(); prs.slide_width = Inches(13.333); prs.slide_height = Inches(7.5)
NAVY=RGBColor(20,52,81); TEAL=RGBColor(26,111,126); WHITE=RGBColor(255,255,255)
PALE=RGBColor(244,248,250); BLUE=RGBColor(231,243,247); GREEN=RGBColor(228,244,232)
AMBER=RGBColor(255,244,215); DARK=RGBColor(34,47,57); GRAY=RGBColor(86,102,112); LINE=RGBColor(193,211,219)

def shape(slide,x,y,w,h,fill,line=LINE,rounded=True):
    s=slide.shapes.add_shape(MSO_SHAPE.ROUNDED_RECTANGLE if rounded else MSO_SHAPE.RECTANGLE, Inches(x), Inches(y), Inches(w), Inches(h))
    s.fill.solid(); s.fill.fore_color.rgb=fill; s.line.color.rgb=line; return s

def text(slide,value,x,y,w,h,size=15,color=DARK,bold=False,font="Aptos",align=PP_ALIGN.LEFT):
    b=slide.shapes.add_textbox(Inches(x),Inches(y),Inches(w),Inches(h)); tf=b.text_frame; tf.clear(); tf.word_wrap=True
    tf.margin_left=tf.margin_right=Inches(.05); tf.margin_top=tf.margin_bottom=Inches(.04)
    p=tf.paragraphs[0]; p.text=value; p.alignment=align; p.font.name=font; p.font.size=Pt(size); p.font.bold=bold; p.font.color.rgb=color
    return b

def section(slide,title,x,y,w,h,fill=PALE):
    s=shape(slide,x,y,w,h,fill)
    text(slide,title,x+.18,y+.12,w-.36,.3,14,TEAL,True,"Aptos Display")
    return s

def bullets(slide,items,x,y,w,h,size=14):
    b=slide.shapes.add_textbox(Inches(x),Inches(y),Inches(w),Inches(h)); tf=b.text_frame; tf.clear(); tf.word_wrap=True
    tf.margin_left=Inches(.06); tf.margin_right=Inches(.04); tf.margin_top=Inches(.02); tf.margin_bottom=Inches(.02)
    for i,item in enumerate(items):
        p=tf.paragraphs[0] if i==0 else tf.add_paragraph(); p.text=item; p.level=0; p.font.name="Aptos"; p.font.size=Pt(size); p.font.color.rgb=DARK
        p.text="• "+p.text; p.space_after=Pt(5)
    return b

def code(slide,lines,x,y,w,h,size=11):
    b=slide.shapes.add_textbox(Inches(x),Inches(y),Inches(w),Inches(h)); tf=b.text_frame; tf.clear(); tf.word_wrap=False
    tf.margin_left=Inches(.06); tf.margin_top=Inches(.02)
    for i,line in enumerate(lines):
        p=tf.paragraphs[0] if i==0 else tf.add_paragraph(); p.text=line; p.font.name="Aptos Mono"; p.font.size=Pt(size); p.font.color.rgb=DARK; p.space_after=Pt(1)
    return b

def slide_phase(num,title,objective,work,tree,purpose,result,status="COMPLETE",in_progress=False,output_title="VERIFIED GENERATED OUTPUTS"):
    slide=prs.slides.add_slide(prs.slide_layouts[6]); slide.background.fill.solid(); slide.background.fill.fore_color.rgb=WHITE
    shape(slide,0,0,13.333,.78,NAVY,NAVY,False)
    text(slide,f"PHASE {num}",.35,.16,1.25,.3,13,WHITE,True)
    text(slide,title,1.55,.1,9.1,.48,24,WHITE,True,"Aptos Display")
    sf=AMBER if in_progress else GREEN; tag=shape(slide,10.85,.17,2.05,.35,sf,sf)
    tf=tag.text_frame; tf.clear(); tf.vertical_anchor=MSO_ANCHOR.MIDDLE
    p=tf.paragraphs[0]; p.text=status; p.alignment=PP_ALIGN.CENTER; p.font.name="Aptos"; p.font.size=Pt(11); p.font.bold=True; p.font.color.rgb=DARK

    section(slide,"OBJECTIVE",.4,1.02,12.53,.85,BLUE)
    text(slide,objective,.62,1.38,12.05,.34,14,DARK)

    section(slide,"WORK PERFORMED",.4,2.05,7.05,3.55,PALE)
    bullets(slide,work,.65,2.52,6.55,2.85,13)

    section(slide,output_title,7.65,2.05,5.28,3.55,PALE)
    code(slide,tree,7.9,2.52,4.8,2.82,11.2)

    fill=AMBER if in_progress else BLUE
    section(slide,"PURPOSE AND RESULT",.4,5.82,12.53,1.05,fill)
    text(slide,purpose+"  "+result,.65,6.19,12.0,.48,12.5,DARK)
    text(slide,"Secure Automotive Event Logging using ARM TrustZone and OP-TEE",.42,7.15,7,.18,8,GRAY)
    text(slide,str(num),12.55,7.13,.3,.18,8,GRAY,align=PP_ALIGN.RIGHT)

slide_phase(1,"OP-TEE Secure World build",
    "Create the Secure World operating system that will later execute trusted CAN-validation logic.",
    ["Selected the Raspberry Pi 3 platform and AArch64 architecture.","Cross-compiled OP-TEE OS on the x86-64 laptop.","Generated the combined and split images required by TF-A.","Verified tee.bin and related OP-TEE output files."],
    ["optee_os/out/arm/core/","├── tee.bin","├── tee.elf","├── tee-header_v2.bin","├── tee-pager_v2.bin","└── tee-pageable_v2.bin","","Boot input: header + pager"],
    "OP-TEE supplies the protected environment for Trusted Applications and security decisions.",
    "The Secure World software compiled successfully; physical execution was not tested in this phase.")

slide_phase(2,"Trusted firmware and bootloader packaging",
    "Build the secure monitor and Normal World bootloader, then package OP-TEE and U-Boot into the Raspberry Pi boot artifact.",
    ["Built U-Boot for Raspberry Pi 3 as the BL33 Normal World payload.","Built Trusted Firmware-A with the OP-TEE dispatcher enabled.","Configured OP-TEE as BL32 and U-Boot as BL33.","Created the FIP and combined it with padded BL1 to form armstub8.bin.","Verified the sizes and checksums of the generated binaries."],
    ["u-boot/","└── u-boot.bin       [BL33]","","trusted-firmware-a/","└── build/rpi3/debug/","    ├── fip.bin","    └── armstub8.bin","        = BL1 + FIP","","FIP: BL32 OP-TEE + BL33 U-Boot"],
    "TF-A initializes the secure state and controls world switching; U-Boot later loads Linux and its Device Tree.",
    "The secure and normal boot payloads were compiled and packaged, but not yet booted on the board.")

slide_phase(3,"ARM64 Linux and Raspberry Pi hardware description",
    "Prepare the Normal World kernel with OP-TEE driver support and compile the existing Raspberry Pi 3 Device Trees.",
    ["Configured and cross-compiled the ARM64 Raspberry Pi 3 Linux kernel.","Enabled CONFIG_TEE=y and CONFIG_OPTEE=y.","Compiled the existing standard Device Tree sources for RPi3 B and B+.","No new hardware or custom Device Tree node was added.","Collected the Image, configuration and DTBs in Step3_Output and recorded checksums."],
    ["Step3_Output/","├── Image             [ARM64 kernel]","├── linux.config      [build config]","└── dtbs/","    ├── bcm2710-rpi-3-b.dtb","    └── bcm2710-rpi-3-b-plus.dtb"],
    "Linux runs Normal World and the enabled driver provides the capability to communicate with OP-TEE.",
    "The kernel and standard DTBs compiled successfully; runtime Linux–OP-TEE communication was not yet verified.")

slide_phase(4,"Source and artifact verification",
    "Establish one complete, consistent and reproducible project tree before building the final Linux user space.",
    ["Selected /home/aslam/Minor_Project/optee_full as the authoritative laptop tree.","Confirmed the matching complete Raspberry Pi tree and excluded the empty duplicate folder.","Compared Git revisions of OP-TEE, Linux, Buildroot, TF-A and U-Boot.","Backed up successful Step 1–3 outputs and recorded SHA-256 checksums.","Verified the supported buildroot, update_rootfs, update_bootfs and image targets from rpi3.mk."],
    ["Step4_Verification/","├── STEP4_COMPLETION.md","└── verified_artifacts/","    ├── optee/tee*.bin + tee.elf","    ├── tf-a/armstub8.bin + fip.bin","    ├── u-boot/u-boot.bin","    └── linux/Image + DTBs + config"],
    "This phase prevented mixed revisions, lost binaries, corrupted outputs and unsupported packaging commands.",
    "One controlled build base was established; no new runtime component was created.")

slide_phase(5,"Buildroot filesystem and OP-TEE user space",
    "Create the ARM64 Linux root filesystem containing the services, libraries, tests, applications, TAs and matching kernel modules.",
    ["Started the official Raspberry Pi 3 Buildroot target with four parallel jobs.","Continued the required Linux kernel and module build.","Generated approximately 1,713 module-related files.","Entered Buildroot host-tool and package compilation.","Build stopped because the configured AArch64 compiler prefix/path does not match the available toolchain.","Final OP-TEE user-space files and rootfs.ext4 are not yet generated."],
    ["CURRENT (partial)","optee_full/","├── module_output/   ~1,713 files","└── out-br/{build, host, target}","","EXPECTED AFTER SUCCESS","out-br/","├── target/{tee-supplicant, libteec}","├── target/{xtest, examples, *.ta}","└── images/rootfs.ext4"],
    "Buildroot is required because the kernel alone has no shell, libraries, OP-TEE clients or Trusted Application files.",
    "Step 5 is incomplete; correct the toolchain configuration, resume the build and verify every expected output.","IN PROGRESS",True,"CURRENT BUILD TREE / EXPECTED TARGET")

prs.save(OUT); print(OUT)
