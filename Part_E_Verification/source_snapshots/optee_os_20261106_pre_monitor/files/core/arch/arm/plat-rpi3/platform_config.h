/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2016, Sequitur Labs Inc. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PLATFORM_CONFIG_H
#define PLATFORM_CONFIG_H

#include <mm/generic_ram_layout.h>

/* Make stacks aligned to data cache line length */
#define STACK_ALIGNMENT		64

#ifdef ARM64
#ifdef CFG_WITH_PAGER
#error "Pager not supported for ARM64"
#endif
#endif /* ARM64 */

/* 16550 UART */
#define CONSOLE_UART_BASE	0x3f215040 /* UART0 */
#define CONSOLE_BAUDRATE	115200
#define CONSOLE_UART_CLK_IN_HZ	19200000

/*
 * SPI0 controller (used by the CAN-FD shield). Bus address 0x7e204000 in
 * the device tree maps to this physical address, same 0x7Exxxxxx ->
 * 0x3Fxxxxxx convention as CONSOLE_UART_BASE above.
 *
 * NOTE: this SoC has no TZASC/TZPC-equivalent memory protection
 * controller ("No secure memory on RPi..." above is the project's own
 * prior finding). Registering this region as MEM_AREA_IO_SEC affects
 * only OP-TEE's own page tables - it does not, and cannot on this
 * hardware, physically prevent a compromised Linux kernel from mapping
 * and accessing the same physical registers via /dev/mem. The actual
 * isolation here is that Linux is simply never given a driver/device-tree
 * node for this controller - a software/configuration boundary, not a
 * hardware-enforced one. See KNOWLEDGE.md's Secure Peripheral Ownership
 * addendum.
 */
#define SPI0_BASE		0x3f204000
#define SPI0_REG_SIZE		0x20

/*
 * GPIO controller. This project's DTB gives spi0 a `cs-gpios` property
 * (GPIO 8 for CS0, active-low) - meaning Linux drives chip-select as a
 * plain GPIO, not via the SPI controller's native CS field. This PTA
 * must do the same or the CAN chip's CS pin is never actually asserted.
 */
#define GPIO_BASE		0x3f200000
#define GPIO_REG_SIZE		0xb4

/*
 * Phase 2 of Secure Peripheral Ownership (roadmap Step 15): routes the
 * CAN chip's shared GPIO bank-0 interrupt (GPIO 24/25) to Secure World
 * as an FIQ, via the VC (peripheral) interrupt controller's FIQ_CONTROL
 * register - completely separate hardware from the SPI0_BASE/GPIO_BASE
 * above, and separate again from the "ARM Local" per-core block below.
 *
 * This is the exact area that caused Incident 1 (KNOWLEDGE.md) the
 * first time this project touched EL3 interrupt routing - see
 * KNOWLEDGE.md's eighth addendum for the full register derivation
 * (cross-checked against Linux's own irq-bcm2835.c, drivers/irqchip/,
 * and this board's live device tree, not written from memory).
 */
#define VC_INTC_BASE               0x3f00b200
#define VC_INTC_REG_SIZE           0x200
#define VC_INTC_FIQ_CONTROL_OFFSET 0x0c
#define VC_INTC_FIQ_ENABLE         0x80
/* GPIO bank 0's shared interrupt = peripheral source 49 (device tree:
 * gpio@7e200000's first interrupts cell is <2 17> -> bank 2, bit 17 ->
 * 32 + 17 = 49, matching Linux's own hwirq_to_fiq() encoding exactly.
 */
#define CAN0_GPIO_FIQ_SOURCE       49

/* ARM Local Peripherals block (per-core control, e.g. GPU_INT_ROUTING
 * selects which physical core receives peripheral FIQs/IRQs) - a THIRD,
 * separate hardware block from the two above.
 */
#define ARM_LOCAL_BASE                    0x40000000
#define ARM_LOCAL_REG_SIZE                0x100
#define ARM_LOCAL_GPU_INT_ROUTING_OFFSET  0x0c
/* GPU_INT_ROUTING bits [3:2] select which physical core receives peripheral
 * FIQs (bits [1:0] select IRQ routing, untouched here). Three separate call
 * sites in this project's own code used to hardcode this to physical core 0
 * via a magic-literal `routing &= ~0xcU`; all three now read this named
 * constant instead.
 *
 * IMPORTANT - this constant identifies whichever physical core permanently
 * owns Secure World / handles the CAN FIQ, which is deliberately NOT the
 * same as "whichever core TF-A treats as boot-primary"
 * (RPI_PRIMARY_CPU, trusted-firmware-a/plat/rpi/rpi3/include/platform_def.h).
 * Under Plan A's hard-pinning design: RPI_PRIMARY_CPU stays at its stock
 * value of 0 (physical core 0 always does the early boot on this hardware
 * regardless of that setting - confirmed empirically, this design does not
 * fight it), while physical core 1 is the one permanently diverted into
 * Secure World (see opteed_enter_secure_server_if_core1() in
 * trusted-firmware-a/services/spd/opteed/opteed_pm.c). So this constant must
 * be 1 - the CAN FIQ must reach the permanent-secure core (1), not the
 * boot-primary/Normal-World core (0). There is no shared header between the
 * two build trees, so this must be kept correct by hand and re-checked
 * whenever either RPI_PRIMARY_CPU or the secure-server target core changes.
 */
#define RPI3_PRIMARY_CORE_POS 1U
#define ARM_LOCAL_GPU_INT_ROUTING_FIQ_MASK  0xcU
#define ARM_LOCAL_GPU_INT_ROUTING_FIQ_SHIFT 2U

/*
 * RPi memory map
 *
 * No secure memory on RPi...
 *
 *
 *    Available to Linux <above>
 *  0x0a00_0000
 *    TA RAM: 16 MiB                          |
 *  0x0842_0000                               | TZDRAM
 *    TEE RAM: 4 MiB (TEE_RAM_VA_SIZE)	      |
 *  0x0840_0000 [ARM Trusted Firmware       ] -
 *  0x0840_0000 [TZDRAM_BASE, BL32_LOAD_ADDR] -
 *    Shared memory: 4 MiB                    |
 *  0x0800_0000                               | DRAM0
 *    Available to Linux                      |
 *  0x0000_0000 [DRAM0_BASE]                  -
 *
 */

#define DRAM0_BASE		0x00000000
#define DRAM0_SIZE		0x40000000

#endif /* PLATFORM_CONFIG_H */
