from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import mm
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, ListFlowable, ListItem

OUT = "/home/aslam/Minor_Project/setp_1.pdf"
navy = colors.HexColor("#153B5B")
teal = colors.HexColor("#176B78")
blue = colors.HexColor("#EAF4F7")
yellow = colors.HexColor("#FFF5D8")
green = colors.HexColor("#EAF6EC")
grid = colors.HexColor("#B6C9D2")

ss = getSampleStyleSheet()
ss.add(ParagraphStyle(name="T", parent=ss["Title"], fontName="Helvetica-Bold", fontSize=24, leading=29, textColor=navy, alignment=TA_CENTER, spaceAfter=12))
ss.add(ParagraphStyle(name="ST", parent=ss["Normal"], fontSize=13, leading=18, textColor=teal, alignment=TA_CENTER, spaceAfter=9))
ss.add(ParagraphStyle(name="H1a", parent=ss["Heading1"], fontSize=17, leading=21, textColor=navy, spaceBefore=12, spaceAfter=8))
ss.add(ParagraphStyle(name="H2a", parent=ss["Heading2"], fontSize=13, leading=17, textColor=teal, spaceBefore=10, spaceAfter=5))
ss.add(ParagraphStyle(name="H3a", parent=ss["Heading3"], fontSize=11, leading=14, textColor=navy, spaceBefore=7, spaceAfter=4))
ss.add(ParagraphStyle(name="B", parent=ss["BodyText"], fontSize=9.4, leading=13.7, spaceAfter=7))
ss.add(ParagraphStyle(name="S", parent=ss["BodyText"], fontSize=8.2, leading=11, textColor=colors.HexColor("#4B5C65")))
ss.add(ParagraphStyle(name="Code2", parent=ss["Code"], fontName="Courier", fontSize=8, leading=10.5, backColor=colors.HexColor("#F1F5F7"), borderColor=grid, borderWidth=.5, borderPadding=7, spaceBefore=5, spaceAfter=8))
ss.add(ParagraphStyle(name="Info", parent=ss["BodyText"], fontSize=9.2, leading=13.5, backColor=blue, borderColor=teal, borderWidth=1, borderPadding=8, spaceBefore=6, spaceAfter=9))
ss.add(ParagraphStyle(name="Warn2", parent=ss["BodyText"], fontSize=9.2, leading=13.5, backColor=yellow, borderColor=colors.HexColor("#CC9418"), borderWidth=1, borderPadding=8, spaceBefore=6, spaceAfter=9))
ss.add(ParagraphStyle(name="Done", parent=ss["BodyText"], fontSize=9.2, leading=13.5, backColor=green, borderColor=colors.HexColor("#4C965D"), borderWidth=1, borderPadding=8, spaceBefore=6, spaceAfter=9))

def P(x, sty="B"):
    return Paragraph(x, ss[sty])

def bullets(items, numbered=False):
    return ListFlowable([ListItem(P(x), leftIndent=12) for x in items], bulletType="1" if numbered else "bullet", leftIndent=20, bulletFontSize=8)

def tab(rows, widths):
    d = [[P(str(c), "S") for c in r] for r in rows]
    t = Table(d, colWidths=widths, repeatRows=1, hAlign="LEFT")
    t.setStyle(TableStyle([
        ("BACKGROUND", (0,0), (-1,0), teal), ("TEXTCOLOR", (0,0), (-1,0), colors.white),
        ("FONTNAME", (0,0), (-1,0), "Helvetica-Bold"), ("GRID", (0,0), (-1,-1), .5, grid),
        ("VALIGN", (0,0), (-1,-1), "TOP"), ("LEFTPADDING", (0,0), (-1,-1), 6),
        ("RIGHTPADDING", (0,0), (-1,-1), 6), ("TOPPADDING", (0,0), (-1,-1), 5),
        ("BOTTOMPADDING", (0,0), (-1,-1), 5), ("ROWBACKGROUNDS", (0,1), (-1,-1), [colors.white, colors.HexColor("#F6F9FA")])]))
    return t

def footer(c, doc):
    c.saveState(); c.setStrokeColor(grid); c.line(17*mm, 13*mm, 193*mm, 13*mm)
    c.setFont("Helvetica", 8); c.setFillColor(colors.HexColor("#52636B"))
    c.drawString(17*mm, 8*mm, "OP-TEE Raspberry Pi 3 — Complete Study Notes")
    c.drawRightString(193*mm, 8*mm, f"Page {doc.page}"); c.restoreState()

doc = SimpleDocTemplate(OUT, pagesize=A4, leftMargin=17*mm, rightMargin=17*mm, topMargin=16*mm, bottomMargin=18*mm, title="OP-TEE Raspberry Pi 3 Complete Study Notes", author="Aslam")
s = []

# Cover
s += [Spacer(1, 32*mm), P("Complete Project Explanation", "T"), P("Steps 1–5: Secure Automotive Event Logging using ARM TrustZone and OP-TEE", "ST"), P("Raspberry Pi 3 Model B / B+ · ARM64/AArch64", "ST"), Spacer(1, 17*mm)]
s.append(tab([["Field", "Details"], ["Student", "Aslam"], ["Prepared", "2 September 2026"], ["Completed", "Steps 1, 2, 3 and 4"], ["In progress", "Step 5 — Buildroot root filesystem"], ["Project tree", "/home/aslam/Minor_Project/optee_full"]], [43*mm, 119*mm]))
s += [Spacer(1, 8*mm), P("<b>Purpose of this document:</b> These notes explain what was done, why each step was necessary, how the steps connect, what each output means, and which claims can or cannot yet be made.", "Info")]

# Overview
s += [PageBreak(), P("1. The Main Project Idea", "H1a"), P("The final objective is to receive automotive CAN packets and validate them in ARM TrustZone's Secure World before releasing approved information to ordinary Linux. Security-sensitive keys, validation rules, critical-event decisions, and tamper-evident logs are intended to remain under OP-TEE control.")]
s.append(P("Sender ECU → CAN bus → CAN controller/transceiver<br/>→ secure-designated GPIO interrupt → secure SPI/CAN receive path<br/>→ Secure World validation<br/>├── critical packet → secure handling + secure log<br/>└── normal accepted packet → controlled result to Linux", "Code2"))
s += [P("The planned checks", "H2a"), bullets(["CAN-ID policy: decide whether an identifier is allowed and whether it is critical.", "HMAC authentication: verify that the packet came from an entity that knows the secret key.", "Counter and freshness checks: reject old or delayed data.", "Replay detection: prevent reuse of a previously valid packet.", "Flood/rate detection: detect excessive packet frequency.", "Classification and logging: keep critical events in Secure World and create a hash-chained tamper-evident log."])]
s += [P("Research-platform limitation", "H2a"), P("Raspberry Pi 3 is useful for demonstrating TrustZone concepts, Secure/Normal World transitions, Trusted Applications, client communication, and secure validation logic. It must not be described as production-grade automotive isolation because the RPi3 OP-TEE port lacks production-grade secure DRAM/peripheral isolation and a complete production secure-boot chain.", "Warn2")]

s += [P("2. TrustZone and the Two Worlds", "H1a")]
s.append(tab([["Secure World", "Normal World"], ["OP-TEE OS", "U-Boot and Linux"], ["Trusted Applications", "Client applications"], ["Protected keys and validation", "Networking, filesystems, shell and ordinary services"], ["Security decisions", "Receives only approved results"]], [81*mm,81*mm]))
s.append(P("Normal application → libteec → Linux OP-TEE driver<br/>→ Secure Monitor Call (SMC) → OP-TEE OS → Trusted Application", "Code2"))
s.append(P("A compile-time result only proves that source code produced binaries. Runtime proof requires the board to boot the binaries and demonstrate real communication across this entire path.", "Info"))

# Boot flow
s += [PageBreak(), P("3. How the Boot Components Connect", "H1a")]
s.append(P("Raspberry Pi Boot ROM / VideoCore firmware<br/>↓<br/>armstub8.bin<br/>├── TF-A BL1: early secure firmware<br/>└── FIP (Firmware Image Package)<br/>　　├── BL32: OP-TEE OS — Secure World<br/>　　└── BL33: U-Boot — Normal World bootloader<br/>　　　　　↓<br/>U-Boot loads Linux Image + correct Device Tree<br/>　　　　　↓<br/>Linux mounts Buildroot root filesystem<br/>　　　　　↓<br/>tee-supplicant + libteec + xtest + applications + TA files", "Code2"))
s.append(tab([["Component", "Why it is required"], ["OP-TEE OS", "Provides the Secure World operating system and executes Trusted Applications."], ["TF-A", "Provides early secure firmware and the Secure Monitor used for world transitions."], ["U-Boot", "Loads the Linux kernel, DTB and boot parameters in Normal World."], ["Linux", "Runs ordinary applications, drivers, storage and the OP-TEE kernel interface."], ["Device Tree", "Describes the Raspberry Pi hardware to Linux."], ["Buildroot filesystem", "Provides commands, libraries, services, clients, tests and TA files after Linux starts."]], [40*mm,122*mm]))

# Step 1
s += [P("4. Step 1 — Build OP-TEE OS", "H1a"), P("<b>Status: complete.</b> In this step, OP-TEE OS was cross-compiled for the Raspberry Pi 3 AArch64 target. The build ran on an x86-64 laptop but generated ARM64 machine code for the Raspberry Pi.")]
s += [P("Why Step 1 was necessary", "H2a"), P("The secure part of the final system needs an operating environment in which Trusted Applications can execute. OP-TEE OS supplies secure sessions, memory management, cryptographic APIs, secure storage support, and controlled communication with Linux.")]
s += [P("What was generated", "H2a")]
s.append(tab([["Output", "Meaning"], ["tee.bin", "Combined OP-TEE binary; verified size 532,228 bytes."], ["tee.elf", "ELF image containing symbols useful for debugging."], ["tee-header_v2.bin", "Header supplied to TF-A for the split OP-TEE image."], ["tee-pager_v2.bin", "Main pager/secure payload content."], ["tee-pageable_v2.bin", "Pageable portion; zero bytes can be valid for this build arrangement."]], [48*mm,114*mm]))
s += [P("What Step 1 proves", "H2a"), P("It proves that OP-TEE compiled successfully for the selected RPi3 platform and produced the BL32 payload files required by TF-A.", "Done"), P("It does <b>not</b> prove that OP-TEE booted on the physical Raspberry Pi, that a TA ran, or that Linux communicated with OP-TEE.", "Warn2")]
s += [P("Presentation answer", "H2a"), P("“I cross-compiled OP-TEE OS for Raspberry Pi 3 using AArch64. OP-TEE will run in TrustZone Secure World and execute security-sensitive Trusted Applications. The build produced tee.bin and the split images required by TF-A. This proves compilation, not physical-board execution.”", "Info")]

# Step 2
s += [PageBreak(), P("5. Step 2 — Build U-Boot and Trusted Firmware-A", "H1a"), P("<b>Status: complete.</b> This step created the secure monitor/firmware layer and the Normal World bootloader, then packaged them with the Step 1 OP-TEE output.")]
s += [P("Trusted Firmware-A", "H2a"), P("TF-A initializes the ARM processor in the secure boot stage, provides the runtime Secure Monitor, and controls transitions between Secure and Normal Worlds. OP-TEE is configured as BL32 and U-Boot as BL33.")]
s += [P("U-Boot", "H2a"), P("U-Boot is the Normal World bootloader. Its later job is to load the ARM64 Linux <font name='Courier'>Image</font>, select the correct Raspberry Pi DTB, provide boot arguments, and start Linux.")]
s.append(P("fip.bin<br/>├── BL32 = OP-TEE OS<br/>└── BL33 = U-Boot<br/><br/>armstub8.bin = TF-A BL1 padded to 0x20000 + fip.bin", "Code2"))
s.append(tab([["Verified output", "Size", "Purpose"], ["u-boot.bin", "540,496 bytes", "Normal World bootloader"], ["fip.bin", "1,147,912 bytes", "Package containing later firmware payloads"], ["armstub8.bin", "1,278,984 bytes", "RPi3 early-boot file containing BL1 and the FIP"]], [45*mm,34*mm,83*mm]))
s += [P("Important packaging point", "H2a"), P("For this TF-A RPi3 build, <font name='Courier'>fip.bin</font> is already appended inside <font name='Courier'>armstub8.bin</font>. Therefore, the FIP should not be copied separately to the boot partition for this selected build flow.", "Info")]
s += [P("What Step 2 proves", "H2a"), P("It proves that U-Boot and TF-A compiled and that OP-TEE and U-Boot could be packaged as BL32 and BL33. It still does not prove that the combined chain boots on the Raspberry Pi.", "Done")]

# Step 3
s += [PageBreak(), P("6. Step 3 — Build Linux and Existing Device Trees", "H1a"), P("<b>Status: complete.</b> The ARM64 Linux kernel source was compiled, and the standard existing Raspberry Pi Device Tree sources in the same Linux tree were compiled into binary DTB files.")]
s += [P("Why Linux is required", "H2a"), P("OP-TEE is not a replacement for the complete Linux environment. Linux runs ordinary applications, networking, storage, processes, device drivers, and the Normal World side of OP-TEE communication. In the final design Linux should receive only approved information after Secure World validation.")]
s += [P("What configuration changed", "H2a"), P("The kernel configuration was prepared for ARM64/Raspberry Pi 3 and the following support was verified:")]
s.append(P("CONFIG_TEE=y<br/>CONFIG_OPTEE=y", "Code2"))
s.append(tab([["Option", "Meaning"], ["CONFIG_TEE=y", "Build Linux's generic Trusted Execution Environment subsystem directly into the kernel."], ["CONFIG_OPTEE=y", "Build the OP-TEE-specific Linux driver directly into the kernel."]], [44*mm,118*mm]))
s += [P("Linux configuration versus Device Tree", "H2a")]
s.append(tab([["Linux configuration", "Device Tree"], ["Selects software features and drivers compiled into Linux.", "Describes the board's physical hardware to Linux."], ["We verified TEE and OP-TEE software support.", "We did not add new hardware or create a new Device Tree."], ["Produces the kernel Image and modules.", "Existing .dts/.dtsi sources are compiled into .dtb files."]], [81*mm,81*mm]))
s += [P("Why compile DTBs when no new hardware was attached?", "H2a"), P("No custom hardware was added in Step 3 and no custom CAN node was created. The standard Device Trees were compiled because a bootable ARM system needs a DTB and because using the DTB produced from the same controlled Linux source revision keeps the kernel/driver/hardware description set reproducible. This was normal compilation of existing source, not a hardware modification.", "Info")]
s.append(P("Existing bcm2710-rpi-3-*.dts/.dtsi<br/>→ Device Tree Compiler (DTC)<br/>→ bcm2710-rpi-3-b.dtb and bcm2710-rpi-3-b-plus.dtb", "Code2"))

s += [P("Outputs and verification", "H2a")]
s.append(tab([["Artifact", "Size", "Reason"], ["Image", "22,993,408 bytes", "Uncompressed ARM64 Linux kernel loaded by U-Boot"], ["linux.config", "215,638 bytes", "Exact reproducible kernel configuration"], ["bcm2710-rpi-3-b.dtb", "33,616 bytes", "Hardware description for RPi3 Model B"], ["bcm2710-rpi-3-b-plus.dtb", "34,235 bytes", "Hardware description for RPi3 Model B+"]], [53*mm,34*mm,75*mm]))
s.append(P("The files were collected under <font name='Courier'>Step3_Output/</font>, then their sizes and SHA-256 checksums were recorded.", "B"))
s += [P("What Step 3 does not prove", "H2a"), P("Enabling the OP-TEE driver only gives Linux the ability to communicate if a compatible Secure World is actually running. Step 3 cannot prove that <font name='Courier'>/dev/tee0</font> exists, that <font name='Courier'>tee-supplicant</font> runs, or that a client can open a TA session.", "Warn2")]
s += [P("Presentation answer", "H2a"), P("“I compiled the ARM64 Linux kernel after enabling the generic TEE subsystem and OP-TEE driver. I also compiled the existing standard Raspberry Pi 3 Device Tree sources. No new hardware was added and the Device Tree was not customized. I verified and preserved the kernel Image, configuration, and both board DTBs. Runtime communication still requires the root filesystem and physical boot.”", "Info")]

# Step4
s += [PageBreak(), P("7. Step 4 — Verification and Organization", "H1a"), P("<b>Status: complete.</b> Step 4 did not add a new runtime component. It created a reliable and reproducible foundation before attempting the complete filesystem and image build.")]
s += [P("What was done", "H2a"), bullets(["Selected the complete laptop build tree: /home/aslam/Minor_Project/optee_full.", "Confirmed the real complete Pi tree: /home/aslam/Minor/optee_project. The Pi's /home/aslam/Minor_Project/optee_full directory was empty and excluded.", "Verified the presence of build, Buildroot, firmware, Linux, OP-TEE OS/client/test/examples, TF-A, and U-Boot.", "Compared component Git revisions between laptop and Pi; they matched.", "Backed up successful Step 1–3 artifacts under Step4_Verification/verified_artifacts.", "Calculated SHA-256 checksums to detect later replacement or corruption.", "Inspected build/rpi3.mk and confirmed official targets instead of guessing commands."])]
s.append(tab([["Supported target", "Purpose"], ["tf-a", "Build/package OP-TEE and U-Boot through TF-A"], ["buildroot", "Build the root filesystem after update_rootfs"], ["update_rootfs", "Install matching kernel modules into Buildroot target"], ["update_bootfs", "Assemble firmware, kernel, DTBs, armstub8.bin and U-Boot environment"], ["sdcard-image", "Generate out/rpi3-sdcard.img"], ["all", "Run the complete dependency and image chain"]], [50*mm,112*mm]))
s += [P("Consistency issue found", "H2a"), P("The laptop <font name='Courier'>tee.bin</font> had been rebuilt after the existing <font name='Courier'>armstub8.bin</font> was packaged. Consequently, the current loose OP-TEE binary may not be the OP-TEE payload inside that older armstub. The supported dependency chain must be rerun before final packaging so OP-TEE, U-Boot, and TF-A correspond.", "Warn2")]
s += [P("Why Step 4 matters", "H2a"), P("It prevents using an empty tree, mixing source revisions, losing successful binaries, packaging mismatched payloads, or using an unsupported command. Step 4 is quality control and reproducibility work, not runtime verification.", "Done")]

# Step5
s += [PageBreak(), P("8. Step 5 — Buildroot Root Filesystem and OP-TEE User Space", "H1a"), P("<b>Status: in progress and currently stopped at a toolchain-path error.</b> Step 5 creates the Linux environment that begins running after the kernel boots.")]
s += [P("Buildroot versus the root filesystem", "H2a")]
s.append(tab([["Buildroot", "Root filesystem"], ["A build system that runs on the laptop.", "The directory/image mounted by Linux as /."], ["Cross-compiles packages for ARM64.", "Contains commands, libraries, configuration and services."], ["Creates images such as rootfs.ext4.", "Runs on the Raspberry Pi after boot."]], [81*mm,81*mm]))
s.append(P("Laptop → Buildroot<br/>├── cross-compile ARM64 programs<br/>├── install matching kernel modules<br/>├── install OP-TEE client/test/example packages<br/>└── generate rootfs.ext4<br/>　　　　　　↓<br/>Raspberry Pi root partition", "Code2"))
s += [P("Why a kernel alone is not enough", "H2a"), P("The Linux <font name='Courier'>Image</font> supplies the kernel, but it does not supply a shell, login, shared libraries, daemon processes, client applications, configuration, or TA files. The root filesystem supplies directories such as <font name='Courier'>/bin</font>, <font name='Courier'>/sbin</font>, <font name='Courier'>/lib</font>, <font name='Courier'>/usr</font>, <font name='Courier'>/etc</font>, and <font name='Courier'>/lib/optee_armtz</font>.")]

s += [P("What has actually been done", "H2a"), bullets(["Started the official rpi3.mk buildroot target with four parallel jobs.", "Rechecked OP-TEE dependencies and existing build outputs.", "Continued the required Linux kernel and module build.", "Generated approximately 1,713 module-related files for installation in the target filesystem.", "Entered the Buildroot package configuration and compilation phase.", "Saved the full output in step5-buildroot-2026-09-01.log.", "Identified the current blocking cross-compiler name/path mismatch."])]
s += [P("Current failure", "H2a"), P("Buildroot stopped because it attempted to execute <font name='Courier'>toolchains/aarch64/bin/aarch64-linux-gnu-gcc</font>, while the available downloaded toolchain exposes the <font name='Courier'>aarch64-none-linux-gnu-gcc</font> prefix. The toolchain configuration/path must be corrected, then the incremental Buildroot target can resume.", "Warn2")]

# Components
s += [PageBreak(), P("9. OP-TEE User-Space Components in Step 5", "H1a")]
s += [P("9.1 tee-supplicant", "H2a"), P("<font name='Courier'>tee-supplicant</font> is a Normal World background daemon. OP-TEE uses it when Secure World needs controlled assistance from Linux, including loading TA files, backing secure-storage objects with normal-world files, RPC services, or plugins.")]
s.append(P("Trusted Application / OP-TEE OS<br/>→ RPC request through Linux OP-TEE driver<br/>→ tee-supplicant<br/>→ normal-world TA file or storage file<br/>→ response returned to Secure World", "Code2"))
s.append(P("tee-supplicant is not trusted merely because it assists OP-TEE. It runs in Normal World. OP-TEE must authenticate/protect security-sensitive content and should not expose secret plaintext to the daemon.", "Info"))

s += [P("9.2 libteec", "H2a"), P("<font name='Courier'>libteec</font> implements the GlobalPlatform-style TEE Client API for Normal World programs. It converts convenient application calls into requests for the Linux OP-TEE kernel driver.")]
s.append(P("TEEC_InitializeContext()<br/>TEEC_OpenSession()<br/>TEEC_InvokeCommand()<br/>TEEC_CloseSession()<br/>TEEC_FinalizeContext()", "Code2"))
s.append(P("For the future CAN project, a Linux client may invoke an approved command through libteec and receive only the result that the secure TA is allowed to release.", "B"))

s += [P("9.3 xtest", "H2a"), P("<font name='Courier'>xtest</font> is the OP-TEE test suite. It exercises client sessions, parameter transfer, shared memory, cryptography, secure storage, error handling, cancellation, timing, random functions, and test TAs. It provides stronger runtime evidence than the existence of tee.bin.")]
s.append(P("xtest → libteec → Linux OP-TEE driver → SMC<br/>→ OP-TEE OS → test TA → result returned to xtest", "Code2"))

s += [P("9.4 Host applications", "H2a"), P("A host application is an ordinary Linux program that opens a session with a particular Trusted Application, invokes commands, sends permitted inputs, and receives approved outputs. Examples include OP-TEE hello-world and secure-storage clients.")]
s += [P("9.5 Trusted Applications (.ta files)", "H2a"), P("A TA contains security-sensitive code executed by OP-TEE. It is normally identified by a UUID and stored as <font name='Courier'>&lt;UUID&gt;.ta</font> in the root filesystem. The host and TA must agree on the UUID, command IDs, parameter types, and shared data structures.")]
s.append(P("Host: TEEC_OpenSession(TA_CAN_UUID)<br/>Host: TEEC_InvokeCommand(CMD_VALIDATE_PACKET)<br/>TA: same UUID + handler for CMD_VALIDATE_PACKET", "Code2"))

s += [P("9.6 Matching kernel modules", "H2a"), P("Kernel modules are separately loadable drivers/features installed under <font name='Courier'>/lib/modules/&lt;kernel-version&gt;</font>. They must come from the same kernel build as the Linux Image. Modules from a different kernel version can fail version checks or behave incompatibly.")]

# Verification
s += [PageBreak(), P("10. When Can Linux–OP-TEE Communication Be Claimed?", "H1a"), P("Not after Step 3 and not merely after compiling the root filesystem. The complete boot chain must run on the board.")]
s.append(tab([["Evidence", "What it proves"], ["tee.bin exists", "OP-TEE compiled"], ["CONFIG_OPTEE=y", "Linux contains the OP-TEE driver"], ["rootfs contains libteec/tee-supplicant", "User-space components were packaged"], ["OP-TEE messages in boot log", "Secure World started"], ["/dev/tee0 and /dev/teepriv0", "Linux detected a TEE interface"], ["tee-supplicant running", "Normal World RPC daemon is active"], ["xtest/example succeeds", "End-to-end Linux/client/driver/SMC/OP-TEE/TA communication works"]], [60*mm,102*mm]))
s += [P("Runtime commands to collect evidence", "H2a")]
s.append(P("dmesg | grep -i -E 'op-tee|optee|tee'<br/>ls -l /dev/tee*<br/>ps | grep tee-supplicant<br/>xtest<br/>optee_example_hello_world", "Code2"))
s.append(P("The currently running Raspberry Pi OS was checked earlier and had no /dev/tee* devices, no OP-TEE boot messages, and no armstub8.bin in the active boot set. Therefore, physical OP-TEE runtime communication has not yet been demonstrated.", "Warn2"))

# Full summary
s += [P("11. Full Step-to-Step Connection", "H1a")]
s.append(tab([["Step", "Main action", "Result", "Boundary"], ["1", "Compile OP-TEE OS", "Secure World binary", "Compilation only"], ["2", "Build TF-A + U-Boot and package BL32/BL33", "armstub8.bin/FIP/U-Boot", "Packaging only"], ["3", "Compile Linux + existing RPi3 DTBs", "Image, config and DTBs", "Driver support only"], ["4", "Verify tree, revisions, artifacts, checksums and targets", "Controlled reproducible base", "No new runtime component"], ["5", "Build modules + Buildroot OP-TEE user space", "Expected rootfs.ext4", "Currently incomplete"]], [13*mm,61*mm,48*mm,40*mm]))
s.append(P("Step 1 gives Secure World.<br/>Step 2 gives the secure monitor and bootloader packaging.<br/>Step 3 gives the Normal World kernel and hardware description.<br/>Step 4 ensures the inputs are correct and reproducible.<br/>Step 5 gives Linux user space and the client/TA runtime files.<br/>Later steps assemble the boot image, boot the board, verify OP-TEE, and implement secure CAN.", "Info"))

# Remaining
s += [PageBreak(), P("12. Remaining Work After the Current Point", "H1a")]
s.append(bullets(["Correct the AArch64 Buildroot toolchain prefix/path and resume the incremental build.", "Verify rootfs.ext4, tee-supplicant, libteec, xtest, host examples, TA files and shared-library dependencies.", "Repackage OP-TEE, U-Boot and TF-A consistently through the supported dependency chain.", "Run update_bootfs to assemble Raspberry Pi firmware, config, kernel, DTB, armstub8.bin and U-Boot environment.", "Generate rpi3-sdcard.img and record its checksum.", "Identify the physical SD-card block device safely before any destructive write.", "Boot the Raspberry Pi with serial capture and verify Secure World plus Linux.", "Run xtest and example client/TA communication.", "Attach/configure the external CAN controller only in the later hardware phase.", "Implement secure SPI/interrupt ownership, CAN validation policies and hash-chained logging.", "Perform functional, replay, freshness, HMAC and flood tests."], numbered=True))

s += [P("13. Common Questions and Correct Answers", "H1a")]
s += [P("Did we add hardware in Step 3?", "H2a"), P("No. We compiled the existing standard RPi3 Device Tree sources. Custom CAN hardware and secure ownership are later work.")]
s += [P("Why rebuild a Device Tree if the Pi already has one?", "H2a"), P("To preserve a controlled kernel-and-DTB set from the same selected source revision. We did not claim that the existing Pi DTB was absent or that hardware had changed.")]
s += [P("Did CONFIG_OPTEE=y prove communication?", "H2a"), P("No. It only built the driver capability into Linux. Runtime detection and an xtest/example success are required.")]
s += [P("Did Step 4 build anything new?", "H2a"), P("No new runtime component. It verified source completeness, revisions, outputs, integrity and supported build targets.")]
s += [P("Is Step 5 complete?", "H2a"), P("No. Kernel/module work progressed and Buildroot started, but the final OP-TEE root filesystem was not generated because the cross-compiler path/prefix did not match.")]

# Oral explanation
s += [PageBreak(), P("14. Complete Oral Explanation", "H1a"), P("“My project aims to validate automotive CAN messages inside ARM TrustZone before releasing approved information to Linux. In Step 1, I cross-compiled OP-TEE OS for Raspberry Pi 3, producing the Secure World payload. In Step 2, I built Trusted Firmware-A and U-Boot. TF-A provides the Secure Monitor, loads OP-TEE as BL32, and packages U-Boot as BL33. For Raspberry Pi 3, the padded TF-A BL1 and FIP are combined into armstub8.bin.", "Info")]
s.append(P("“In Step 3, I compiled a 64-bit Linux kernel and enabled CONFIG_TEE and CONFIG_OPTEE so the Normal World kernel contains the OP-TEE interface. I also compiled the existing standard Raspberry Pi 3 Device Tree sources into DTBs. No new hardware was attached and the Device Tree was not customized. I preserved the Linux Image, configuration and DTBs, but this still did not prove runtime communication.", "Info"))
s.append(P("“In Step 4, I selected the complete authoritative project tree, compared laptop and Pi source revisions, backed up successful outputs, recorded checksums, and verified the official Raspberry Pi build targets. This provided a consistent base and revealed that some loose OP-TEE and packaged TF-A artifacts had different build times.", "Info"))
s.append(P("“In Step 5, I started Buildroot to create the Linux root filesystem. The root filesystem must contain matching kernel modules, tee-supplicant, libteec, xtest, host applications and Trusted Applications. tee-supplicant provides normal-world RPC services, libteec is the client API used by applications, xtest checks the full communication path, and TA files execute security-sensitive commands in OP-TEE. Kernel modules were built and Buildroot started, but the final rootfs is not complete because the expected AArch64 compiler prefix does not match the available toolchain. After fixing that, I will resume Buildroot, assemble the boot image, boot the board, and verify end-to-end communication before implementing secure CAN.”", "Info"))

s += [P("15. Final Status", "H1a")]
s.append(tab([["Area", "Status"], ["OP-TEE OS compilation", "Complete"], ["TF-A and U-Boot compilation/packaging", "Complete"], ["ARM64 Linux and standard RPi3 DTBs", "Complete"], ["Source/artifact verification and backup", "Complete"], ["Buildroot root filesystem", "In progress — toolchain mismatch"], ["Boot partition and SD-card image", "Not complete"], ["Physical OP-TEE boot and xtest", "Not complete"], ["Secure CAN implementation", "Not started"]], [103*mm,59*mm]))
s.append(P("The strongest correct claim today is: <b>the secure/normal-world build components through Step 4 are compiled and verified as artifacts; Step 5 has progressed into Buildroot but the usable runtime filesystem and physical OP-TEE communication are not yet complete.</b>", "Done"))
s.append(P("Evidence locations: Step3_Output/, Step4_Verification/, CURRENT_STATUS_AND_NEXT_STEPS.md, RPI3_SECURE_CAN_FULL_ROADMAP.md, and step5-buildroot-2026-09-01.log.", "S"))

doc.build(s, onFirstPage=footer, onLaterPages=footer)
print(OUT)
