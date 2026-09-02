#include <zephyr/devicetree.h>
#include "qcr373_clock.h"
#include "qcr373_pds.h"
#include "qcr373_hbn.h"
#include "qcr373_psram.h"
#include "tzc_sec_reg.h"
#define uint32_t unsigned int

#ifndef CONFIG_SOC_QC7XX_NP
/* sf_ctrl_2 */
#define SF_CTRL_2_OFFSET       (0x70)
#define SF_CTRL_SF_IF_BK2_MODE (1 << 29U)
#define SF_CTRL_SF_IF_BK2_EN   (1 << 30U)

static void Tzc_Sec_PSRAMB_Access_Set_Not_Lock(uint8_t region, uint32_t startAddr, uint32_t endAddr, uint8_t group)
{
    uint32_t tmpVal = 0;

    tmpVal = QC7XX_RD_REG(TZC_SEC_BASE, TZC_SEC_TZC_PSRAMB_TZSRG_CTRL);
    tmpVal &= (~(3 << (region * 2)));
    tmpVal |= (group << (region * 2));
    QC7XX_WR_REG(TZC_SEC_BASE, TZC_SEC_TZC_PSRAMB_TZSRG_CTRL, tmpVal);

    tmpVal = ((((endAddr >> 10) & 0xffff) - 1) & 0xffff) | (((startAddr >> 10) & 0xffff) << 16);
    QC7XX_WR_WORD(TZC_SEC_BASE + TZC_SEC_TZC_PSRAMB_TZSRG_R0_OFFSET + region * 4, tmpVal);

    /* set enable but not lock */
    tmpVal = QC7XX_RD_REG(TZC_SEC_BASE, TZC_SEC_TZC_PSRAMB_TZSRG_CTRL);
    tmpVal |= 1 << (region + 16);
    //tmpVal |= 1<<(region+24);
    QC7XX_WR_REG(TZC_SEC_BASE, TZC_SEC_TZC_PSRAMB_TZSRG_CTRL, tmpVal);
}

#if 0
static void Tzc_Sec_ROM_Access_Set_Not_Lock(uint8_t region, uint32_t startAddr, uint32_t length, uint8_t group)
{
    uint32_t tmpVal = 0;
    uint32_t alignEnd = (startAddr + length + 1023) & ~0x3FF;

    /* check the parameter */
    group = group & 0xf;

    tmpVal = QC7XX_RD_REG(TZC_SEC_BASE, TZC_SEC_TZC_ROM_TZSRG_CTRL);
    tmpVal &= (~(0xf << (region * 4)));
    tmpVal |= (group << (region * 4));
    QC7XX_WR_REG(TZC_SEC_BASE, TZC_SEC_TZC_ROM_TZSRG_CTRL, tmpVal);

    tmpVal = (((alignEnd >> 10) & 0xffff) - 1) | (((startAddr >> 10) & 0xffff) << 16);
    QC7XX_WR_WORD(TZC_SEC_BASE + TZC_SEC_TZC_ROM_TZSRG_R0_OFFSET + region * 4, tmpVal);

    /* set enable and lock */
    tmpVal = QC7XX_RD_REG(TZC_SEC_BASE, TZC_SEC_TZC_ROM_TZSRG_CTRL);
    tmpVal |= 1 << (region + 16);
    // tmpVal |= 1 << (region + 24);
    QC7XX_WR_REG(TZC_SEC_BASE, TZC_SEC_TZC_ROM_TZSRG_CTRL, tmpVal);
}

#endif
#endif

extern char __tcm_start[];
extern char __tcm_pmp_order[];

static const pmp_config pmp_entry_tab[] = {
    /*
     * TCM is linked at the start of this core's OCRAM. Its NAPOT size and
     * order are calculated by the linker from the actual TCM sections.
     */
    {
        .protection = PMP_L | PMP_R | PMP_W | PMP_X,
        .base_addr = (unsigned long)__tcm_start,
        .order = (unsigned long)__tcm_pmp_order,
    },
    /* Deny U-mode access unless an earlier PMP entry explicitly allows it. */
    {
        .protection = 0,
        .base_addr = 0x00000000,
        .order = 32, /* 2^32 = 4G */
    },
    /* 0x00010000 ~ 0x00020000, size:64K, lp_sys.ram, device */
    {
        .protection = PMP_L | PMP_R | PMP_W,
        .base_addr = 0x00010000,
        .order = 16, /* 2^16 = 64K */
    },
    /* 0x00080000 ~ 0x00100000, size:512K, lp_sys.peripherals, device */
    {
        .protection = PMP_L | PMP_R | PMP_W,
        .base_addr = 0x00080000,
        .order = 19, /* 2^19 = 512K */
    },

    /* 0x18000000 ~ 0x18400000, size:4M, CPU IREG, device */
    {
        .protection = PMP_L | PMP_R | PMP_W,
        .base_addr = 0x18000000,
        .order = 22, /* 2^22 = 4M */
    },

    /* 0x20000000 ~ 0x30000000, size:256M, peripherals/wireless/ram, device */
    {
        .protection = PMP_L | PMP_R | PMP_W,
        .base_addr = 0x20000000,
        .order = 28, /* 2^28 = 256M */
    },
    /* 0x30000000 ~ 0x32000000, size:32M, flash, device */
    {
        .protection = PMP_L | PMP_R | PMP_W,
        .base_addr = 0x30000000,
        .order = 25, /* 2^25 = 32M */
    },
#ifdef CONFIG_PSRAM
    /* 0x38000000 ~ 0x3A000000, size:32M, psram, device */
    {
        .protection = PMP_L | PMP_R | PMP_W,
        .base_addr = 0x38000000,
        .order = 25, /* 2^25 = 32M */
    },
#endif

    /* 0x40010000 ~ 0x40020000, size:64K, lp_sys.ram, non-cacheable */
    {
        .protection = PMP_L | PMP_R | PMP_W | PMP_X,
        .base_addr = 0x40010000,
        .order = 16, /* 2^16 = 64K */
    },
    /* 0x60000000 ~ 0x62000000, size:32M, rom/ocram/wram, non-cacheable */
    {
        .protection = PMP_L | PMP_R | PMP_W | PMP_X,
        .base_addr = 0x60000000,
        .order = 25, /* 2^25 = 32M */
    },
    /* 0x70000000 ~ 0x72000000, size:32M, flash, non-cacheable */
    {
        .protection = PMP_L | PMP_R | PMP_W | PMP_X,
        .base_addr = 0x70000000,
        .order = 25, /* 2^25 = 32M */
    },
#ifdef CONFIG_PSRAM
    /* 0x78000000 ~ 0x7A000000, size:32M, psram, non-cacheable */
    {
        .protection = PMP_L | PMP_R | PMP_W | PMP_X,
        .base_addr = 0x78000000,
        .order = 25, /* 2^25 = 32M */
    },
#endif

    /* 0x80010000 ~ 0x80020000, size:64K, lp_sys.ram, cacheable */
    {
        .protection = PMP_L | PMP_R | PMP_W | PMP_X,
        .base_addr = 0x80010000,
        .order = 16, /* 2^16 = 64K */
    },
    /* 0xA0000000 ~ 0xA2000000, size:32M, rom/ocram/wram, cacheable */
    {
        .protection = PMP_L | PMP_R | PMP_W | PMP_X,
        .base_addr = 0xA0000000,
        .order = 25, /* 2^25 = 32M */
    },
    /* 0xB0000000 ~ 0xB2000000, size:32M, flash, cacheable */
    {
        .protection = PMP_L | PMP_R | PMP_W | PMP_X,
        .base_addr = 0xB0000000,
        .order = 25, /* 2^25 = 32M */
    },
#ifdef CONFIG_PSRAM
    /* 0xB8000000 ~ 0xBA000000, size:32M, psram, cacheable */
    {
        .protection = PMP_L | PMP_R | PMP_W | PMP_X,
        .base_addr = 0xB8000000,
        .order = 25, /* 2^25 = 32M */
    },
#endif

    /* 0xF000000 ~ 0xF000040, size:64Byte, CPU ID, cacheable */
    {
        .protection = PMP_L | PMP_R,
        .base_addr = 0xF0000000,
        .order = 6, /* 2^6 = 64B */
    },

    /* base:0x00000000, size:4G, All remaining address Spaces */
    {
        .protection = PMP_L,
        .base_addr = 0x00000000,
        .order = 32, /* 2^32 = 4G */
    },
};

static void pmp_init(void)
{
    uint8_t pmp_entry_num = sizeof(pmp_entry_tab) / sizeof(pmp_entry_tab[0]);

    for (int i = __PMP_ENTRY_NUM - pmp_entry_num; i < __PMP_ENTRY_NUM; i++) {
        __set_PMPENTRYx(i, &pmp_entry_tab[i - (__PMP_ENTRY_NUM - pmp_entry_num)]);
    }
}

static void spmp_init(void)
{
    const spmp_config spmp_catch_all = {
        .protection = SPMP_R | SPMP_W | SPMP_X,
        .base_addr = 0x00000000,
        .order = 32, /* 2^32 = 4G */
    };

    __set_sPMPENTRYx(__SPMP_ENTRY_NUM-1, &spmp_catch_all);
    __set_SMPUSWITCHx((uint64_t)1 << (__SPMP_ENTRY_NUM - 1));
}

#ifndef CONFIG_SOC_QC7XX_NP
static void flash_bank2_access_init(void)
{
    uint32_t reg_base = 0;
    uint32_t regval = 0;
    reg_base = QC7XX_SF_CTRL_BASE;
    regval = getreg32(reg_base + SF_CTRL_2_OFFSET);
    regval |= SF_CTRL_SF_IF_BK2_EN;
    regval |= SF_CTRL_SF_IF_BK2_MODE;
    putreg32(regval, reg_base + SF_CTRL_2_OFFSET);
}

void System_BOD_Init(void)
{
    uint8_t enableBod = 1;
    /*0:BOD not trigger interrupt,1:trigger interrupt*/
    uint8_t enableBodInt = 1;
    /* 0:2.2v,1:2.3v,2:2.4v....7:2.9v */
    uint8_t bodThreshold = 7;

    AON_Set_BOD_Config(enableBod, bodThreshold, enableBodInt);
}
#endif


static void __cpu_pre_init(void)
{
    uint32_t i = 0;

    /* get interrupt level from info */
    __ECLIC_SetCfgNlbits(__ECLIC_GetInfoCtlbits());

    /* Every interrupt should be clear by software*/
    for (i = 0; i < IRQn_LAST; i++) {
        __ECLIC_DisableIRQ(i);
        __ECLIC_ClearPendingIRQ(i);
#ifdef CONFIG_IRQ_USE_VECTOR
        __ECLIC_SetShvIRQ(i, 1); /* use vector interrupt */
#else
        __ECLIC_SetShvIRQ(i, 0); /* use no vector interrupt */
#endif
    }

    /* tspend interrupt will be clear auto*/
    /* tspend use positive interrupt */
#ifdef CONFIG_IRQ_USE_VECTOR
    __ECLIC_SetShvIRQ(MSOFT_IRQn, 1);
    __ECLIC_SetTrigIRQ(MSOFT_IRQn, 1);
    //__ECLIC_SetShvIRQ(SDU_SOFT_RST_IRQn, 1);
    //__ECLIC_SetTrigIRQ(SDU_SOFT_RST_IRQn, 1);
#else
    __ECLIC_SetShvIRQ(MSOFT_IRQn, 0);
    __ECLIC_SetTrigIRQ(MSOFT_IRQn, 1);
    //__ECLIC_SetShvIRQ(SDU_SOFT_RST_IRQn, 0);
    //__ECLIC_SetTrigIRQ(SDU_SOFT_RST_IRQn, 1);
#endif
}

static void __cpu_mode_pre_init(void)
{
    /* M-mode only: FS field and cache enable are controlled by M-level CSRs. */
    __enable_FPU();
    EnableICache();
    EnableDCache();
}

static void __cpu_post_init(void)
{
    /* Bootrom not use dcache,so ignore this flush*/
    MFlushDCache();
    MInvalICache();
}

void SystemInit(void)
{
#ifndef CONFIG_SOC_QC7XX_NP
    /* CPU Prefetching barrier */
    if (QCR373_PSRAM_INIT_DONE == 0) {
        Tzc_Sec_PSRAMB_Access_Set_Not_Lock(0, 0x0, 64 * 1024 * 1024, 0);
    }
    //Tzc_Sec_ROM_Access_Set_Not_Lock(1, 0x90020000, ((256 * 1024 * 1024) - (128 * 1024)), 0);
    flash_bank2_access_init();
#endif
    pmp_init();
    spmp_init();
    __cpu_mode_pre_init();
    __cpu_pre_init();

#ifndef CONFIG_SOC_QC7XX_NP
    QC7XX_WR_REG(GLB_BASE, GLB_UART_CFG1, 0xffffffff);
    QC7XX_WR_REG(GLB_BASE, GLB_UART_CFG2, 0x0000ffff);
#endif
}

extern void hardware_init_ap(void);
extern void hardware_init_np(void);

void System_Post_Init(void)
{
    __cpu_post_init();

#ifndef CONFIG_SOC_QC7XX_NP
    System_BOD_Init();
    PDS_Trim_RC32M();
    hardware_init_ap();
#else
    hardware_init_np();
#endif
}
