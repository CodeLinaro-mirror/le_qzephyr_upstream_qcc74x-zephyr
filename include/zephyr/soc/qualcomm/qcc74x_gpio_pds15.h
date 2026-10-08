/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_GPIO_PDS15_H_
#define ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_GPIO_PDS15_H_

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal bridge between the QCC74x GPIO driver and PDS15 backend. */
#if defined(CONFIG_QCC74X_PM_PDS15) && defined(CONFIG_GPIO_QCC74X)
/* Check SoC ownership conflicts before changing a candidate wake pin. */
int gpio_qc7xx_pds15_wake_pin_validate(uint32_t io_pin);
/* Read-only admission check; does not arm wake hardware or consume status. */
int gpio_qc7xx_pds15_preflight(void);
int gpio_qc7xx_pds15_prepare(void);
int gpio_qc7xx_pds15_resume(uint64_t *wake_bits, uint64_t *edge_bits);
void gpio_qc7xx_pds15_dispatch_wake(void);
#else
static inline int gpio_qc7xx_pds15_wake_pin_validate(uint32_t io_pin)
{
	(void)io_pin;
	return -ENOTSUP;
}

static inline int gpio_qc7xx_pds15_preflight(void)
{
	return 0;
}

static inline int gpio_qc7xx_pds15_prepare(void)
{
	return 0;
}

static inline int gpio_qc7xx_pds15_resume(uint64_t *wake_bits,
					  uint64_t *edge_bits)
{
	if (wake_bits != NULL) {
		*wake_bits = 0U;
	}
	if (edge_bits != NULL) {
		*edge_bits = 0U;
	}

	return 0;
}

static inline void gpio_qc7xx_pds15_dispatch_wake(void)
{
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SOC_QUALCOMM_QCC74X_GPIO_PDS15_H_ */
