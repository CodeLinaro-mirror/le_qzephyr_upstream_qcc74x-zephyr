/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
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
	 * when mcause.interrupt is set. Keep the restored interrupt bit aligned
	 * with the live trap entry type: exception-entered traps return with it
	 * clear, while interrupt-entered traps return with it set.
	 *
	 * When returning from an interrupt to an exception-saved frame, clear
	 * MPIL so the resumed exception context restores MIL to zero.
	 */
	addi a0, a0, -__struct_arch_esf_soc_context_OFFSET
	lw t0, __struct_arch_esf_mcause_OFFSET(a0)
	csrr t2, mcause
	bltz t2, 2f
	li t1, ~(1 << RISCV_MCAUSE_IRQ_POS)
	and t0, t0, t1
	j 3f
2:
	bltz t0, 3f
	li t1, 0xFF00FFFF
	and t0, t0, t1
	li t1, 1 << RISCV_MCAUSE_IRQ_POS
	or t0, t0, t1
3:
	sw t0, __struct_arch_esf_mcause_OFFSET(a0)
#endif /* CONFIG_CLIC_SUPPORT_INTERRUPT_LEVEL */

	ret

#endif /* CONFIG_RISCV_SOC_CONTEXT_SAVE */
