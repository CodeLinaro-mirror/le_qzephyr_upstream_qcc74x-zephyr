/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */



#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/offsets.h>
    .section .init
    .align  2
    .globl  __qc7xx_start
    .type   __qc7xx_start, %function
__qc7xx_start:
.option push
.option norelax
    la      gp, __global_pointer$
.option pop
    csrci   mstatus, 8

    lui a0, 0x20056
    lw  a1, 0xb0(a0)
    lui a0, 0x40280
    lui a2, 0xffff0
    and a1, a1, a2
    beq a0, a1, 1f
    j .
1:

    /* PSRAM is not executable until start_load() finishes the runtime copy. */
#ifdef CONFIG_PSRAM_COPY_CODE
    la      a0, __qc7xx_early_trap
#else
    la      a0, _isr_wrapper
#endif
    ori     a0, a0, 3
    csrw    mtvec, a0
#ifdef CONFIG_IRQ_USE_VECTOR
    /* mtvt: for all vector mode IRQs */
    la      a0, _qcc74x_vector_table
    csrw    mtvt, a0
#endif
#if defined(__riscv_flen)
    /* FP: initial state */
    csrr    t0, mstatus
    li      t1, ~0x6000
    and     t0, t0, t1
    li      t1, 0x2000
    or      t0, t0, t1
    csrw    mstatus, t0
    /* csrwi   fcsr, 0 */
#endif

    /*
     * Initially, setup stack pointer to
     * z_interrupt_stacks + __z_interrupt_stack_SIZEOF
     */
    la sp, z_interrupt_stacks
    li t0, __z_interrupt_stack_SIZEOF
    add sp, sp, t0
    csrw    mscratch, sp

    /* Load data section removed */

    /* Clear bss section removed */

    call     SystemInit

    /* start load code to itcm like. */
    call     start_load

#ifdef CONFIG_PSRAM_COPY_CODE
    /* Runtime code is now copied, including the CLIC exception entry. */
    fence.i
    la      a0, _isr_wrapper
    ori     a0, a0, 3
    csrw    mtvec, a0
#endif

    call     System_Post_Init

    call     z_cstart

__exit:
    j      __exit

#ifdef CONFIG_PSRAM_COPY_CODE
    /* No kernel or PSRAM state may be used before start_load() completes. */
    .balign 64
__qc7xx_early_trap:
    csrci   mstatus, 8
1:
    j       1b
#endif
