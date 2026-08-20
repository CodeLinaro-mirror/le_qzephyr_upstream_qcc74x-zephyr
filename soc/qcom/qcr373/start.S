#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/offsets.h>

/* Nuclei N900 extended CSRs / bits (from NMSIS riscv_encoding.h) */
.equ CSR_MMISC_CTL,              0x7D0
.equ CSR_MCOUNTINHIBIT,          0x320
.equ MMISC_CTL_NMI_CAUSE_FFF,     (1 << 9)
.equ MMISC_CTL_BPU,              (1 << 3)
.equ MSTATUS_VS,                 0x00000600
.equ MSTATUS_VS_INITIAL,         0x00000200
    .section .init
    .align  2
    .globl  __qcr373_start
    .type   __qcr373_start, %function
__qcr373_start:
.option push
.option norelax
    la      gp, __global_pointer$
.option pop
    /* Disable Global Interrupt (MSTATUS.MIE) */
    csrci   mstatus, 8

1:

    /*
     * N900: make NMI share entry with exceptions by setting MMISC_CTL.NMI_CAUSE_FFF.
     * Then program MTVEC and switch interrupt processing mode to ECLIC.
     */
    li      t0, MMISC_CTL_NMI_CAUSE_FFF
    csrs    CSR_MMISC_CTL, t0

    /* mtvec: for all exceptions and non-vector mode IRQs */
    la      a0, _isr_wrapper
    csrw    mtvec, a0
    /* Set the interrupt processing mode to ECLIC mode (mtvec low bits = 0b11) */
    li      t0, 0x3f
    csrc    mtvec, t0
    li      t0, 0x3
    csrs    mtvec, t0
#ifdef CONFIG_IRQ_USE_VECTOR
    /* mtvt: for all vector mode IRQs */
    la      a0, _qcr373_vector_table
    csrw    mtvt, a0
#endif

#if defined(__riscv_vector)
    /* Enable Vector, and set state to initial */
    li      t0, MSTATUS_VS
    csrc    mstatus, t0
    li      t0, MSTATUS_VS_INITIAL
    csrs    mstatus, t0
#endif

    /* Enable mcycle and minstret counter */
    csrci   CSR_MCOUNTINHIBIT, 0x5
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

    call     System_Post_Init

    /* BPU cold bringup need time, enable BPU before entering Zephyr */
    li      t0, MMISC_CTL_BPU
    csrs    CSR_MMISC_CTL, t0

    call     z_cstart

__exit:
    j      __exit
