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


/* Imports */
GTEXT(_isr_wrapper)
	.global  _qcr373_vector_table
	.section .exception_vectors.text
	.balign   0x40
	.type     _qcr373_vector_table, @function

_qcr373_vector_table:
	.option push
	.option norvc
	.rept CONFIG_NUM_IRQS
	j _isr_wrapper
	.endr
