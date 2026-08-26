/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * All rights reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 *
 * Contains code under the following license:
 *
 * Not a contribution.
 *
 * Copyright (c) Zephyr Project members and individual contributors.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/offsets.h>
#include <zephyr/toolchain.h>
#include <zephyr/arch/riscv/irq.h>

#ifdef CONFIG_RISCV_SOC_CONTEXT_SAVE
GTEXT(__soc_save_context)
GTEXT(__soc_restore_context)
#endif

#ifdef CONFIG_RISCV_SOC_CONTEXT_SAVE

SECTION_FUNC(exception.other, __soc_save_context)
	ret

SECTION_FUNC(exception.other, __soc_restore_context)
#ifdef CONFIG_CLIC_SUPPORT_INTERRUPT_LEVEL
	/*
	 * On Nuclei ECLIC, MRET restores mintstatus.MIL from mcause.MPIL only
	 * when mcause.interrupt is set.
	 *
	 * Only force mcause.interrupt for ECALLM scheduling frames. Do not
	 * modify exception-class frames (e.g. ECALLU), which must keep
	 * mcause.interrupt cleared.
	 */
	addi a0, a0, -__struct_arch_esf_soc_context_OFFSET
	lw t0, __struct_arch_esf_mcause_OFFSET(a0)
	li t1, CONFIG_RISCV_MCAUSE_EXCEPTION_MASK
	and t2, t0, t1
	li t1, RISCV_EXC_ECALLM
	bne t2, t1, 1f
	li t1, 1 << RISCV_MCAUSE_IRQ_POS
	or t0, t0, t1
	sw t0, __struct_arch_esf_mcause_OFFSET(a0)
1:
#endif /* CONFIG_CLIC_SUPPORT_INTERRUPT_LEVEL */

	ret

#endif /* CONFIG_RISCV_SOC_CONTEXT_SAVE */
