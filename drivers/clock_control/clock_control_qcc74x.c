/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define DT_DRV_COMPAT qcom_qcc74x_clock_controller

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <qc7xx_pwm_v2.h>
#include <zephyr/drivers/clock_control/qcc7xx_clk_common_ll.h>
#include <zephyr/drivers/clock_control/qcc7xx_clk_dig_ll.h>
#include <zephyr/drivers/clock_control/qcc7xx_clk_pwm_ll.h>
#include <zephyr/drivers/clock_control/qcc7xx_clk_sec_ll.h>
#include <zephyr/drivers/clock_control/qcc7xx_clk_spi_ll.h>
#include <zephyr/drivers/clock_control/qcc7xx_clk_timer_ll.h>
#include <zephyr/drivers/clock_control/qcc7xx_clk_uart_ll.h>
#ifdef CONFIG_SOC_SERIES_QCC74X
#include <zephyr/dt-bindings/clock/qcc74x-clock.h>
#include <qcc74x_glb.h>
#include <qcc74x_hbn.h>
#endif
#ifdef CONFIG_SOC_SERIES_QCC75X
#include <zephyr/dt-bindings/clock/qcc75x-clock.h>
#include <qcc75x_glb.h>
#include <qcc75x_hbn.h>
#endif

#define LOG_LEVEL CONFIG_CLOCK_CONTROL_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(clock_control_qcc74x, CONFIG_CLOCK_CONTROL_LOG_LEVEL);

#include <stdbool.h>
#include <qc7xx_clock.h>
#include <qc7xx_peri.h>

#define QC7XX_CAM_MAX_CLK_DIV 0x3U

#define QC7XX_IS_DBI_CLK_SOURCE(type)                                                              \
	(((type) == QC7XX_DBI_CLK_MCU_MUXPLL_160M) || ((type) == QC7XX_DBI_CLK_XCLK))

#ifdef CONFIG_SOC_SERIES_QCC74X
#define QC7XX_IS_SDH_CLK_SOURCE(type)                                                              \
	(((type) == QC7XX_SDH_CLK_WIFIPLL_96M) || ((type) == QC7XX_SDH_CLK_TOP_AUPLL_DIV5))
#endif
#ifdef CONFIG_SOC_SERIES_QCC75X
#define QC7XX_IS_SDH_CLK_SOURCE(type) ((type) == QC7XX_SDH_CLK_WIFIPLL_96M)
#endif

struct clock_control_qcc74x_config {
	const struct pinctrl_dev_config *pincfg; /* Optional GPIO_CLK_IN pin definition */
	const uint8_t f32k_src;                  /* The 32k clock source index */
	const uint8_t dig_src;                   /* DIG clock source index */
	const uint32_t gpio_clk_freq;            /* Optional: external clock frequency [Hz] */
	const uint8_t mcu_clk_src;               /* System clock source */
	const uint8_t xtal_type;                 /* XTAL type 40M/32M/None etc. */
	const uint8_t wifipll_option;            /* Wifi PLL option 960M/OFF */
	const uint8_t audiopll_option;           /* Audio PLL option 491M/451M ... /OFF */

	/* Timers and watchdog clock sources - we need to fetch them from DT
	 * because this driver is initialized at PRE_KERNEL_1 stage at which
	 * any information in GLB registers is not valid (since drivers didn't
	 * yet initialize) */
	const uint8_t timer0_src;
	const uint8_t timer1_src;
	const uint8_t wdt_src;
	const uint8_t mtimer_src;
	const uint32_t mtimer_freq;
};

static enum clock_control_status clock_control_qcc74x_get_status(const struct device *dev,
							       clock_control_subsys_t sys)
{
	ARG_UNUSED(dev);
	uint8_t status = qc7xx_peripheral_clock_status_get((uint32_t)sys);
	if (status == 1) {
		return CLOCK_CONTROL_STATUS_ON;
	}
	return CLOCK_CONTROL_STATUS_OFF;
}

static int clock_control_qcc74x_on(const struct device *dev, clock_control_subsys_t sys)
{
	if (QC7XX_PERIPHERAL_JENC == (uintptr_t)sys) {
		GLB_PER_Clock_UnGate(GLB_AHB_CLOCK_MJPEG);
		return 0;
	}

	if (QC7XX_PERIPHERAL_CAN == (uintptr_t)sys) {
		PERIPHERAL_CLOCK_CAN_ENABLE();
		return 0;
	}

	enum clock_control_status status = clock_control_qcc74x_get_status(dev, sys);

	if (status == CLOCK_CONTROL_STATUS_ON) {
		return -EALREADY;
	}
	qc7xx_peripheral_clock_control((uintptr_t)sys, 1);

	return 0;
}

static int clock_control_qcc74x_off(const struct device *dev, clock_control_subsys_t sys)
{
	enum clock_control_status status = clock_control_qcc74x_get_status(dev, sys);

	if (status == CLOCK_CONTROL_STATUS_ON) {
		qc7xx_peripheral_clock_control((uintptr_t)sys, 0);
	}

	return 0;
}

static int clock_control_qcc74x_get_rate(const struct device *dev, clock_control_subsys_t sys,
				       uint32_t *rate)
{
	__ASSERT((rate != NULL), "Received rate NULL!");
	ARG_UNUSED(dev);
	uint8_t type;
	uint32_t clk_sys = (uint32_t)sys;
	uint8_t tmr_wdg_clk_src = QC7XX_TIMER_CLK_NO;
	const struct clock_control_qcc74x_config *const config = dev->config;

	/* Runtime check */
	if (rate == NULL) {
		return -EINVAL;
	}

	switch (clk_sys) {
	case QC7XX_PERIPHERAL_GPADC0:
		type = QC7XX_DEVICE_TYPE_ADC;
		break;
#ifdef CONFIG_SOC_SERIES_QCC74X
	case QC7XX_PERIPHERAL_GPDAC0:
		type = QC7XX_DEVICE_TYPE_DAC;
		break;
#endif
	case QC7XX_PERIPHERAL_UART0:
		ATTR_FALLTHROUGH();
	case QC7XX_PERIPHERAL_UART1:
		type = QC7XX_DEVICE_TYPE_UART;
		break;
#ifdef CONFIG_SOC_SERIES_QCC75X
	case QC7XX_PERIPHERAL_UART2:
		type = QC7XX_DEVICE_TYPE_UART;
		break;
#endif
	case QC7XX_PERIPHERAL_SPI0:
		type = QC7XX_DEVICE_TYPE_SPI;
		break;
	case QC7XX_PERIPHERAL_I2C0:
		ATTR_FALLTHROUGH();
	case QC7XX_PERIPHERAL_I2C1:
		type = QC7XX_DEVICE_TYPE_I2C;
		break;
	case QC7XX_PERIPHERAL_SF_CTRL:
		type = QC7XX_DEVICE_TYPE_FLASH;
		break;
	case QC7XX_PERIPHERAL_IR:
		type = QC7XX_DEVICE_TYPE_IR;
		break;
	case QC7XX_PERIPHERAL_SEC_ENG:
		type = QC7XX_DEVICE_TYPE_SEC_PKA;
		break;
	case QC7XX_PERIPHERAL_SDH0:
		type = QC7XX_DEVICE_TYPE_SDH;
		break;
	case QC7XX_PERIPHERAL_CAM0:
		type = QC7XX_DEVICE_TYPE_CAMERA;
		break;
	case QC7XX_PERIPHERAL_DBI:
		type = QC7XX_DEVICE_TYPE_DBI;
		break;
	case QC7XX_PERIPHERAL_I2S0:
		type = QC7XX_DEVICE_TYPE_I2S;
		break;
	case QC7XX_PERIPHERAL_RTC:
		type = QC7XX_DEVICE_TYPE_RTC;
		break;
	case QC7XX_PERIPHERAL_PWM0:
		*rate = qcc7xx_clk_pwm_ll_get_rate();
		return 0;
	case QC7XX_PERIPHERAL_TIMER0:
		ATTR_FALLTHROUGH();
	case QC7XX_PERIPHERAL_TIMER1:
		ATTR_FALLTHROUGH();
	case QC7XX_PERIPHERAL_WATCHDOG:
		tmr_wdg_clk_src = qcc7xx_clk_timer_ll_get_clk_source(clk_sys);

		if (tmr_wdg_clk_src == QC7XX_TIMER_CLK_GPIO) {
			*rate = config->gpio_clk_freq;
		} else {
			*rate = qcc7xx_clk_timer_ll_get_rate(clk_sys);
		}

		return 0;
	case QC7XX_PERIPHERAL_CAN:
		*rate = qc7xx_clk_get_system_clock(QC7XX_SYSTEM_XCLK);
		return 0;
	default:
		return -ENOTSUP;
	}
	*rate = qc7xx_clk_get_peripheral_clock(type, 0);

	return 0;
}

static int clock_control_qcc74x_configure(const struct device *dev, clock_control_subsys_t sys,
					void *data)
{
	ARG_UNUSED(dev);

	uint32_t clk_sys = (uint32_t)sys;

	if (data == NULL && clk_sys != QC7XX_PERIPHERAL_USB20) {
		LOG_ERR("NULL pointer provided as data");
		return -EINVAL;
	}

	switch (clk_sys) {

	case QC7XX_PERIPHERAL_AUADC0:
		struct qcc74x_clk_settings *audio_settings_adc = (struct qcc74x_clk_settings *)data;

		if (audio_settings_adc->enable) {
#ifdef CONFIG_SOC_SERIES_QCC74X
			GLB_Config_AUDIO_PLL_To_491P52M();
#endif
			GLB_PER_Clock_UnGate(GLB_AHB_CLOCK_AUDIO);
		}

		GLB_Set_Audio_ADC_CLK(audio_settings_adc->enable, audio_settings_adc->clk_div);
		break;
	case QC7XX_PERIPHERAL_AUDAC0:
		struct qcc74x_clk_settings *audio_settings_dac = (struct qcc74x_clk_settings *)data;

		if (audio_settings_dac->enable) {
#ifdef CONFIG_SOC_SERIES_QCC74X
			GLB_Config_AUDIO_PLL_To_491P52M();
#endif
			GLB_PER_Clock_UnGate(GLB_AHB_CLOCK_AUDIO);
		}

		GLB_Set_Audio_SOLO_CLK(audio_settings_dac->enable, audio_settings_dac->clk_div);
		break;
	case QC7XX_PERIPHERAL_CAM0:
		struct qcc74x_clk_settings *cam_clk_settings = (struct qcc74x_clk_settings *)data;

		if (IS_GLB_CAM_CLK_TYPE(cam_clk_settings->clk_src) &&
		    cam_clk_settings->clk_div <= QC7XX_CAM_MAX_CLK_DIV) {
			GLB_Set_CAM_CLK(cam_clk_settings->enable, cam_clk_settings->clk_src,
					cam_clk_settings->clk_div);
		} else {
			LOG_ERR("Invalid clock source: %u or div: %u for CAM",
				cam_clk_settings->clk_src, cam_clk_settings->clk_div);
			return -EINVAL;
		}
		break;
	case QC7XX_PERIPHERAL_DMA0:
		uint8_t clk_channel = *(uint8_t *)data;
		if (IS_GLB_DMA_CLK_ID_TYPE(clk_channel)) {
			GLB_Set_DMA_CLK(CLOCK_ENABLE, clk_channel);
		} else {
			LOG_ERR("Invalid clock channel for DMA: %u", clk_channel);
			return -EINVAL;
		}
		break;
	case QC7XX_PERIPHERAL_GPADC0:
		struct qcc74x_clk_settings *adc_clk_settings = (struct qcc74x_clk_settings *)data;
		if (IS_GLB_ADC_CLK_TYPE(adc_clk_settings->clk_src) &&
		    adc_clk_settings->clk_div <= 0x3F) {
			GLB_Set_ADC_CLK(CLOCK_ENABLE, adc_clk_settings->clk_src,
					adc_clk_settings->clk_div);
		} else {
			LOG_ERR("Invalid clock source: %u or div: %u for ADC",
				adc_clk_settings->clk_src, adc_clk_settings->clk_div);
			return -EINVAL;
		}
		break;
	case QC7XX_PERIPHERAL_I2C0:
		ATTR_FALLTHROUGH();
	case QC7XX_PERIPHERAL_I2C1:
		uint8_t clk_source = *(uint8_t *)data;
		if (IS_GLB_I2C_CLK_TYPE(clk_source)) {
			GLB_Set_I2C_CLK(CLOCK_ENABLE, clk_source, 0);
		} else {
			LOG_ERR("Invalid clock source: %u for I2C", clk_source);
			return -EINVAL;
		}
		break;
	case QC7XX_PERIPHERAL_PWM0:
		struct qcc74x_clk_settings *pwm_settings = (struct qcc74x_clk_settings *)data;
		if (QC7XX_IS_PWM_CLK_SOURCE(pwm_settings->clk_src) && (pwm_settings->clk_div > 0)) {
			qcc7xx_clk_pwm_ll_set(CLOCK_ENABLE, pwm_settings->clk_src,
					      pwm_settings->clk_div);
		} else {
			LOG_ERR("Invalid clock source: %u and/or div: %u for PWM",
				pwm_settings->clk_src, pwm_settings->clk_div);
			return -EINVAL;
		}
		break;
	case QC7XX_PERIPHERAL_I2S0:
		struct qcc74x_clk_settings *i2s_settings = (struct qcc74x_clk_settings *)data;
		if (IS_GLB_I2S_DI_REF_CLK_TYPE(i2s_settings->clk_src) &&
		    IS_GLB_I2S_DO_REF_CLK_TYPE(i2s_settings->clk_src2) &&
		    i2s_settings->clk_div <= 0x3F) {
			GLB_Set_I2S_CLK(i2s_settings->enable, i2s_settings->clk_div,
					i2s_settings->clk_src, i2s_settings->clk_src2);
		} else {
			LOG_ERR("Invalid clock settings for I2S "
				" inref:%u or outref:%u or div:%u",
				i2s_settings->clk_src, i2s_settings->clk_src2, i2s_settings->clk_div);
			return -EINVAL;
		}
		break;

	case QC7XX_PERIPHERAL_SPI0:
		struct qcc74x_clk_settings *spi_settings = (struct qcc74x_clk_settings *)data;
		if (QC7XX_IS_SPI_CLK_SOURCE(spi_settings->clk_src) &&
		    spi_settings->clk_div <= QC7XX_SPI_MAX_CLK_DIV) {
			GLB_Set_SPI_CLK(spi_settings->enable, spi_settings->clk_src,
					spi_settings->clk_div);
		} else {
			LOG_ERR("Invalid clock settings for SPI "
				" clk_src: %u or clk_div:%u",
				spi_settings->clk_src, spi_settings->clk_div);
			return -EINVAL;
		}
		break;

	case QC7XX_PERIPHERAL_SDH0:
		struct qcc74x_clk_settings *sdh_settings = (struct qcc74x_clk_settings *)data;
		if (QC7XX_IS_SDH_CLK_SOURCE(sdh_settings->clk_src)) {
			GLB_Set_SDH_CLK(CLOCK_ENABLE, sdh_settings->clk_src, sdh_settings->clk_div);
		} else {
			LOG_ERR("Invalid clock source: %u for SDH", sdh_settings->clk_src);
			return -EINVAL;
		}
		break;
	case QC7XX_PERIPHERAL_DBI:
		struct qcc74x_clk_settings *dbi_settings = (struct qcc74x_clk_settings *)data;
		if (QC7XX_IS_DBI_CLK_SOURCE(dbi_settings->clk_src)) {
			GLB_Set_DBI_CLK(CLOCK_ENABLE, dbi_settings->clk_src, dbi_settings->clk_div);
		} else {
			LOG_ERR("Invalid clock source: %u for DBI", dbi_settings->clk_src);
			return -EINVAL;
		}
		break;
	case QC7XX_PERIPHERAL_SEC0:
		/* Clock can be enabled by entropy driver already */
		if (qc7xx_peripheral_clock_status_get((uint8_t)QC7XX_PERIPHERAL_SEC0) != 1) {
			clk_source = *(uint8_t *)data;
			if (QC7XX_IS_SEC_CLK_SOURCE(clk_source)) {
				GLB_Set_PKA_CLK_Sel(clk_source);
			} else {
				LOG_ERR("Invalid clock source: %u for SEC", clk_source);
				return -EINVAL;
			}
		}
		break;
	case QC7XX_PERIPHERAL_IR:
		struct qcc74x_clk_settings *ir_settings = (struct qcc74x_clk_settings *)data;

		if (ir_settings->clk_div > 0x3F || !IS_GLB_IR_CLK_SRC_TYPE(ir_settings->clk_src)) {
			LOG_ERR("Invalid clock divider: %u or clk_src: %u for IR", ir_settings->clk_div,
				ir_settings->clk_src);
			return -EINVAL;
		}
		GLB_Set_IR_CLK(ir_settings->enable, ir_settings->clk_src, ir_settings->clk_div);
		break;

	case QC7XX_PERIPHERAL_UART0:
		ATTR_FALLTHROUGH();
	case QC7XX_PERIPHERAL_UART1:
		struct qcc74x_clk_settings *uart_settings = (struct qcc74x_clk_settings *)data;
		if (IS_HBN_UART_CLK_TYPE(uart_settings->clk_src) &&
		    uart_settings->clk_div <= QC7XX_UART_MAX_CLK_DIV) {
			GLB_Set_UART_CLK(uart_settings->enable, uart_settings->clk_src,
					 uart_settings->clk_div);
		} else {
			LOG_ERR("Invalid clock settings for UART "
				" clk_src: %u or clk_div:%u",
				uart_settings->clk_src, uart_settings->clk_div);
			return -EINVAL;
		}
		break;
	case QC7XX_PERIPHERAL_USB20:
		GLB_Set_USB_CLK_From_WIFIPLL(1);
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int clock_control_qcc74x_init_dig_clk(const struct device *dev)
{
	const struct clock_control_qcc74x_config *const config = dev->config;
	uint8_t clk_src = config->dig_src;
	uint32_t xtal_freq = 0U;

	switch (clk_src) {
	case QC7XX_DIG_CLK_WIFIPLL_32M:
		/* TODO: In the future it should be considered to
		 * check if wifi PLL is on or off.  */
		break;
	case QC7XX_DIG_CLK_XCLK:
		HBN_Get_Xtal_Value(&xtal_freq);
		if (!xtal_freq) {
			LOG_ERR("XTAL not available!");
			return -EINVAL;
		}
		break;
	case QC7XX_DIG_CLK_AUPLL:
		LOG_INF("DIG clock source is AUPLL");
		/* TODO: In the future it should be considered to
		 * check if wifi audio is on or off.  */
		break;
	default:
		LOG_WRN("Invalid DIG clock selection: %u \n"
			"Using default: ( 1 [XCLK])",
			config->dig_src);
		HBN_Get_Xtal_Value(&xtal_freq);
		if (!xtal_freq) {
			LOG_ERR("XTAL not available!");
			return -EINVAL;
		}
		clk_src = QC7XX_DIG_CLK_WIFIPLL_32M;
	}

	GLB_Set_DIG_CLK_Sel(clk_src);

	return 0;
}

static void clock_control_qcc74x_init_32k_clk(const struct device *dev)
{
	const struct clock_control_qcc74x_config *const config = dev->config;
	uint8_t clk_src = config->f32k_src;
	uint16_t dig_div = 0U;
	uint32_t dig_src_rate = 0U;

	switch (clk_src) {
	case QC7XX_HBN_32K_RC:
#ifdef CONFIG_SOC_SERIES_QCC74X
		/* Important: We are disabling XTAL32 in multiple places
		 * of this function in order to save the power consumption
		 * from powering unused crystal. If there will be a need
		 * to use it in different way - those lines can be deleted
		 */

		/* 32K RC is probably always on */
		/* Make sure that XTAL is turned off */
		HBN_Power_Off_Xtal_32K();
#endif
		break;
	case QC7XX_HBN_32K_XTAL:
#ifdef CONFIG_SOC_SERIES_QCC74X
		/* When XTAL is used GPIO 16 and 17 are unavailable */
		HBN_Power_On_Xtal_32K();
#endif
		/* Wait for XTAL to stabilize */
		k_busy_wait(USEC_PER_MSEC * 100);
		break;
	case QC7XX_HBN_32K_DIG:
#ifdef CONFIG_SOC_SERIES_QCC74X
		/* Make sure that XTAL is turned off */
		HBN_Power_Off_Xtal_32K();
#endif
		dig_src_rate = qcc7xx_clk_dig_ll_get_rate();

		if (!dig_src_rate) {
			LOG_ERR("Could not get dig source clock value!");
			return;
		}

		dig_div = dig_src_rate / 32768;

		if (dig_div > QC7XX_DIG_32K_MAX_CLK_DIV) {
			LOG_ERR("Could not obtain desired frequency: 32768Hz"
				" With source frequency %u and divider: %u",
				dig_src_rate, QC7XX_DIG_32K_MAX_CLK_DIV);
			return;
		}

		/* Enable DIG_32 clock */
		GLB_Set_DIG_32K_CLK(ENABLE, 0, dig_div);
		break;
	default:
		LOG_WRN("Invalid 32k clock selection: %u \n"
			"Using default: ( 0 [RC 32k])",
			config->f32k_src);
#ifdef CONFIG_SOC_SERIES_QCC74X
		/* Make sure that XTAL is turned off */
		HBN_Power_Off_Xtal_32K();
#endif
		clk_src = QC7XX_HBN_32K_RC;
		break;
	}

	/* Select clock source */
	HBN_32K_Sel(clk_src);
}

#ifdef CONFIG_DAC_QCC74X
static int clock_control_qcc74x_init_dig_512k_clk(void)
{
	uint8_t dig_div = 0;
	uint32_t dig_src_rate = 0;

	dig_src_rate = qcc7xx_clk_dig_ll_get_rate();

	if (!dig_src_rate) {
		LOG_ERR("Could not get dig source clock value!");
		return -EIO;
	}

	dig_div = dig_src_rate / 512000U;

	if (dig_div > QC7XX_DIG_512K_MAX_CLK_DIV) {
		LOG_ERR("Could not obtain desired frequency: 512kHz"
			" With source frequency %u and divider: %u",
			dig_src_rate, QC7XX_DIG_512K_MAX_CLK_DIV);
		return -EINVAL;
	}

	GLB_Set_DIG_512K_CLK(ENABLE, ENABLE, dig_div);

	return 0;
}
#endif

/**
 * @brief Configure system clocks
 *
 * @param dev pointer to clock_control driver instance.
 */
static void initialize_system_clocks(const struct device *dev)
{
	const struct clock_control_qcc74x_config *const config = dev->config;
	uint8_t pll_types_on = 0;

#ifdef CONFIG_SOC_SERIES_QCC74X
	if (config->audiopll_option != QC7XX_AUDIO_PLL_OFF) {
		pll_types_on |= GLB_PLL_AUPLL;
	}
#endif
	if (config->wifipll_option != QC7XX_WIFI_PLL_OFF) {
		pll_types_on |= GLB_PLL_WIFIPLL;
	}

	GLB_Power_On_XTAL_And_PLL_CLK(config->xtal_type, pll_types_on);

	GLB_Set_MCU_System_CLK(config->mcu_clk_src);

	if (config->xtal_type == QC7XX_XTAL_24M || config->xtal_type == QC7XX_XTAL_32M ||
	    config->xtal_type == QC7XX_XTAL_38P4M || config->xtal_type == QC7XX_XTAL_40M ||
	    config->xtal_type == QC7XX_XTAL_26M) {
		HBN_Set_MCU_XCLK_Sel(HBN_MCU_XCLK_XTAL);
	} else {
		HBN_Set_MCU_XCLK_Sel(HBN_MCU_XCLK_RC32M);
	}

#ifdef CONFIG_SOC_SERIES_QCC74X
	switch (config->audiopll_option) {
	case QC7XX_AUDIO_PLL_491P52M:
		GLB_Config_AUDIO_PLL_To_491P52M();
		break;
	case QC7XX_AUDIO_PLL_451P58M:
		GLB_Config_AUDIO_PLL_To_451P58M();
		break;
	case QC7XX_AUDIO_PLL_400M:
		GLB_Config_AUDIO_PLL_To_400M();
		break;
	case QC7XX_AUDIO_PLL_245P76M:
		/* GLB_Config_AUDIO_PLL_To_240M and
		 * GLB_Config_AUDIO_PLL_To_245P76M are
		 * not present in qcc74x_glb.h file */
		ATTR_FALLTHROUGH();
	case QC7XX_AUDIO_PLL_240M:
		ATTR_FALLTHROUGH();
	case QC7XX_AUDIO_PLL_384M:
		GLB_Config_AUDIO_PLL_To_384M();
		break;
	case QC7XX_AUDIO_PLL_OFF:
		GLB_Power_Off_AUPLL();
		break;
	}
#endif
}

/**
 * @brief Initialize and configure mtimer clock source and divider.
 *
 * This function resolves the mtimer source clock frequency from the
 * devicetree-provided source selector, validates the target mtimer
 * frequency, computes the hardware divider and programs mtimer clocking.
 * IMPORTANT: if desired frequency will not be a direct divisor of source
 *            frequency - there will be small difference between the
 *            system expectations and real outcome.
 *
 * @param dev Pointer to clock controller device instance.
 *
 * @retval 0       Mtimer clock configured successfully.
 * @retval -EINVAL Invalid mtimer source/frequency settings or divider out of range.
 */
static int initialize_mtimer(const struct device *dev)
{
	const struct clock_control_qcc74x_config *const config = dev->config;
	uint32_t mtimer_source_hz = 0U;
	uint32_t mtimer_div = 0U;

	mtimer_source_hz = qcc7xx_clk_common_Clock_Get_mtimer_src_freq(config->mtimer_src);
	if (mtimer_source_hz == 0U) {
		LOG_ERR("Unsupported mtimer source: %u", config->mtimer_src);
		return -EINVAL;
	}

	if (config->mtimer_freq == 0U || mtimer_source_hz < config->mtimer_freq) {
		LOG_ERR("Invalid mtimer clock config source=%uHz target=%uHz", mtimer_source_hz,
			config->mtimer_freq);
		return -EINVAL;
	}

	mtimer_div = (uint32_t)(mtimer_source_hz / config->mtimer_freq - 1U);
	if (mtimer_div > 0x3FFU) {
		LOG_ERR("mtimer divider out of range: %u", mtimer_div);
		return -EINVAL;
	}

	if ((mtimer_source_hz % config->mtimer_freq) != 0U) {
		LOG_WRN("mtimer target %uHz is not an exact divisor of source %uHz",
			config->mtimer_freq, mtimer_source_hz);
	}

	CPU_Set_MTimer_CLK(ENABLE, config->mtimer_src, (uint16_t)mtimer_div);

	return 0;
}

/**
 * @brief Disable all peripheral clocks.
 */
static void clock_control_qcc74x_disable_all_peripheral_clocks()
{
	GLB_Set_Audio_ADC_CLK(DISABLE, 0);
	GLB_Set_Audio_SOLO_CLK(DISABLE, 0);
	GLB_Set_ADC_CLK(DISABLE, QC7XX_ADC_CLK_AUPLL, 0);
	GLB_Set_ADC_CLK(DISABLE, QC7XX_ADC_CLK_XCLK, 0);
	GLB_Set_I2C_CLK(DISABLE, QC7XX_I2C_CLK_XCLK, 0);
	GLB_Set_I2C_CLK(DISABLE, QC7XX_I2C_CLK_BCLK, 0);
	GLB_Set_SPI_CLK(DISABLE, QC7XX_SPI_CLK_MCU_MUXPLL_160M, 0);
	GLB_Set_SPI_CLK(DISABLE, QC7XX_SPI_SPI_CLK_XCLK, 0);
	qc7xx_peripheral_clock_control((uintptr_t)QC7XX_PERIPHERAL_SDH0, 0);
	GLB_Set_SDH_CLK(DISABLE, QC7XX_SDH_CLK_WIFIPLL_96M, 0);
#ifdef CONFIG_SOC_SERIES_QCC74X
	GLB_Set_SDH_CLK(DISABLE, GLB_SDH_CLK_TOP_AUPLL_DIV5, 0);
#endif
	qc7xx_peripheral_clock_control((uintptr_t)QC7XX_PERIPHERAL_DBI, 0);
	GLB_Set_DBI_CLK(DISABLE, QC7XX_DBI_CLK_MCU_MUXPLL_160M, 0);
	GLB_Set_DBI_CLK(DISABLE, QC7XX_DBI_CLK_XCLK, 0);
	qc7xx_peripheral_clock_control((uintptr_t)QC7XX_PERIPHERAL_EMAC0, 0);
	qc7xx_peripheral_clock_control((uintptr_t)QC7XX_PERIPHERAL_SEC0, 0);
	GLB_Set_DMA_CLK(DISABLE, QC7XX_DMA0_CLK_CH0);
	GLB_Set_DMA_CLK(DISABLE, QC7XX_DMA0_CLK_CH1);
	GLB_Set_DMA_CLK(DISABLE, QC7XX_DMA0_CLK_CH2);
	GLB_Set_DMA_CLK(DISABLE, QC7XX_DMA0_CLK_CH3);
	GLB_Set_IR_CLK(DISABLE, QC7XX_IR_CLK_XCLK, 0);
	GLB_Set_UART_CLK(DISABLE, QC7XX_UART_CLK_MCU_BCLK, 0);
	GLB_Set_UART_CLK(DISABLE, QC7XX_UART_CLK_MUXPLL_160M, 0);
	GLB_Set_UART_CLK(DISABLE, QC7XX_UART_CLK_XCLK, 0);
	qcc7xx_clk_timer_ll_set_source(QC7XX_PERIPHERAL_TIMER0, QC7XX_TIMER_CLK_NO);
	qcc7xx_clk_timer_ll_set_source(QC7XX_PERIPHERAL_TIMER1, QC7XX_TIMER_CLK_NO);
	qcc7xx_clk_timer_ll_set_source(QC7XX_PERIPHERAL_WATCHDOG, QC7XX_TIMER_CLK_NO);
	GLB_Set_DIG_32K_CLK(DISABLE, 0, 0);
	GLB_Set_DIG_512K_CLK(DISABLE, DISABLE, 0);
	GLB_Set_CAM_CLK(DISABLE, QC7XX_CAM_CLK_XCLK, 0);
	GLB_Set_CAM_CLK(DISABLE, QC7XX_CAM_CLK_WIFIPLL_96M, 0);
	GLB_Set_CAM_CLK(DISABLE, QC7XX_CAM_CLK_TOP_AUPLL_DIV5, 0);

	qc7xx_peripheral_clock_control((uintptr_t)QC7XX_PERIPHERAL_USB20, 0);
	qc7xx_peripheral_clock_control((uintptr_t)QC7XX_PERIPHERAL_SDU0, 0);
	GLB_PER_Clock_Gate(GLB_AHB_CLOCK_MJPEG);
	GLB_Set_USB_CLK_From_WIFIPLL(0);
}

/**
 * @brief Initialize and configure dig clock and 32k clock.
 *
 * This function initializes dig clock and 32k clock according
 * to the settings fetched from devicetree.
 *
 * @param dev Pointer to clock controller device instance.
 *
 * @retval 0       Initialization successful.
 * @retval -EINVAL Invalid configuration received.
 */
static int clock_control_qcc74x_init_dig_and_32k_clock(const struct device *dev)
{
	int ret;
	bool dig_clk_not_initialized = false;
	bool f32k_needs_dig_clk = false;
	const struct clock_control_qcc74x_config *const config = dev->config;

	ret = clock_control_qcc74x_init_dig_clk(dev);

	dig_clk_not_initialized = (config->dig_src == QC7XX_DIG_CLK_OFF || ret != 0);
	f32k_needs_dig_clk = (config->f32k_src == QC7XX_HBN_32K_DIG);
	if (dig_clk_not_initialized && f32k_needs_dig_clk) {
		LOG_ERR("f32k clock requires DIG_32 clock"
			" and its not turned on");
		return -EINVAL;
	}
	clock_control_qcc74x_init_32k_clk(dev);

#ifdef CONFIG_DAC_QCC74X
	if (dig_clk_not_initialized) {
		LOG_ERR("DAC requires DIG_512 clock"
			" and its not turned on");
		return -EINVAL;
	}

	ret = clock_control_qcc74x_init_dig_512k_clk();
	if (ret != 0) {
		LOG_ERR("Failed to initialize DIG_512K clock. ret = %d", ret);
		return -EINVAL;
	}
#endif

	return 0;
}

/**
 * @brief Configure optional GPIO clock input for timers/watchdog.
 *
 * If any timer/watchdog source is set to @ref QC7XX_TIMER_CLK_GPIO, this
 * function verifies that pin is defined in the default pinctrl state and
 * routes that pin as the MCU timer GPIO clock input.
 *
 * The default pinctrl state is then applied for the clock controller node.
 * Missing pinctrl state (-ENOENT) is accepted because GPIO clock input is
 * optional unless a GPIO timer source is requested.
 *
 * @param dev Pointer to the clock controller device.
 *
 * @retval 0 GPIO clock routing/pinctrl configuration completed.
 * @retval -EINVAL GPIO timer source was requested without a declared pin.
 * @retval <0 Propagated error from pinctrl configuration.
 */
static int clock_control_manage_gpio_clk(const struct device *dev)
{
	int ret = 0;
	const struct clock_control_qcc74x_config *const config = dev->config;

	if (config->timer0_src == QC7XX_TIMER_CLK_GPIO ||
	    config->timer1_src == QC7XX_TIMER_CLK_GPIO || config->wdt_src == QC7XX_TIMER_CLK_GPIO) {
		if (config->pincfg->states->pin_cnt == 0) {
			LOG_ERR("GPIO clock selected but no pin has been declared!");
			return -EINVAL;
		}

		GLB_Sel_MCU_TMR_GPIO_Clock(QC7XX_PIN_GET_NUM(
			config->pincfg->states[PINCTRL_STATE_DEFAULT].pins[0].pinmux));
	}

	ret = pinctrl_apply_state(config->pincfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0 && ret != -ENOENT) {
		/* -ENOENT is fine since clock input is optional */
		LOG_ERR("Failed to configure pinctrl for timer/watchdog err: %d", ret);
		return ret;
	}

	return 0;
}

/**
 * @brief Initialize clock control by disabling all clock source variants by default.
 */
static int clock_control_qcc74x_init(const struct device *dev)
{
	int ret = 0U;

	/* XTAL/PLL's and MCU system clock configuration */
	initialize_system_clocks(dev);

	/* Mtimer is responsible for system delays - so it need to be
	 * initialized just after system clocks */
	ret = initialize_mtimer(dev);
	if (ret != 0) {
		return ret;
	}

	/* Disable clocks by default to have known init state */
	clock_control_qcc74x_disable_all_peripheral_clocks();

#ifdef CONFIG_VIDEO_QCC74X
	/* Fix for initialization errors in the capture_to_lvgl sample.
	 * These errors occur because this driver disables the CAM clock
	 * while the video driver uses POST_KERNEL priority. Without an active
	 * external clock during sensor detection, the camera remains
	 * unresponsive. Although the system recovers and functions correctly,
	 * this change prevents the initial error messages. */
	GLB_Set_CAM_CLK(ENABLE, QC7XX_CAM_CLK_WIFIPLL_96M, 3);
#endif

	/* Initialize DIG and 32k clock according to values fetched from DT */
	ret = clock_control_qcc74x_init_dig_and_32k_clock(dev);
	if (ret != 0) {
		return ret;
	}

	/* Verify if GPIO source clock has been chosen from DT and
	 * if pin was provided - enable it */
	ret = clock_control_manage_gpio_clk(dev);

	return ret;
}

/**
 * @brief Clock control API structure for Opal.
 */
static DEVICE_API(clock_control, clock_control_qcc74x_api) = {
	.on = clock_control_qcc74x_on,
	.off = clock_control_qcc74x_off,
	.get_rate = clock_control_qcc74x_get_rate,
	.get_status = clock_control_qcc74x_get_status,
	.configure = clock_control_qcc74x_configure,
};

/**
 * @brief Device tree definition for Opal clock controller.
 */
#define QCC74X_CLOCK_INIT(n)                                                                         \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	static const struct clock_control_qcc74x_config qcc74x_config_##n = {                          \
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                       \
		.f32k_src = (uint8_t)DT_INST_PROP(n, f32k_clk_src),                                \
		.dig_src = (uint8_t)DT_INST_PROP(n, dig_clk_src),                                  \
		.gpio_clk_freq = DT_INST_PROP_OR(n, clk_in_freq, 0),                               \
		.timer0_src = (uint8_t)DT_CLOCKS_CELL(DT_NODELABEL(timer0), source),               \
		.timer1_src = (uint8_t)DT_CLOCKS_CELL(DT_NODELABEL(timer1), source),               \
		.wdt_src = (uint8_t)DT_CLOCKS_CELL(DT_NODELABEL(wdt0), source),                    \
		.mtimer_src = (uint8_t)DT_CLOCKS_CELL(DT_NODELABEL(mtimer), source),               \
		.mtimer_freq = DT_PROP(DT_NODELABEL(mtimer), clock_frequency),                     \
		.mcu_clk_src = (uint8_t)DT_CLOCKS_CELL(DT_NODELABEL(cpu0), source),                \
		.xtal_type = (uint8_t)DT_INST_PROP(n, xtal_type),                                  \
		.wifipll_option = (uint8_t)DT_INST_PROP(n, wifi_pll_type),                         \
		.audiopll_option = (uint8_t)DT_INST_PROP(n, audio_pll_type),                       \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, clock_control_qcc74x_init, NULL, NULL, &qcc74x_config_##n,            \
			      PRE_KERNEL_1, CONFIG_CLOCK_CONTROL_INIT_PRIORITY,                    \
			      &clock_control_qcc74x_api);

/* Call the macro for every "okay" node in the devicetree */
DT_INST_FOREACH_STATUS_OKAY(QCC74X_CLOCK_INIT)
