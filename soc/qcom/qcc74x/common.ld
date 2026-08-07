/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * CONFIG_FLASH_XIP_ADDRESS
 * CONFIG_FLASH_XIP_LENGTH
 * CONFIG_WRAM_ADDRESS
 * CONFIG_WRAM_LENGTH
 * CONFIG_RAM_ADDRESS
 * CONFIG_RAM_LENGTH
 * CONFIG_PSRAM_ADDRESS
 * CONFIG_PSRAM_LENGTH
 */

#include <zephyr/devicetree.h>
#include <zephyr/linker/sections.h>
#include <zephyr/linker/devicetree_regions.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/linker/linker-tool.h>
#include <zephyr/linker/linker-devnull.h>

#define ROMABLE_REGION rom
#define RAMABLE_REGION ram
#define NONECACHE_RAM_REGION ram_nocache
#define WIFI_RAM_REGION ram_wifi

/* 4M */
#ifndef CONFIG_FLASH_XIP_ADDRESS
#define CONFIG_FLASH_XIP_ADDRESS 0xA0000000
#endif
#ifndef CONFIG_FLASH_XIP_LENGTH
#define CONFIG_FLASH_XIP_LENGTH 4096 * 1024
#endif

/* 4M */
#define CONFIG_PSRAM_ADDRESS 0xA8000000
#ifndef CONFIG_PSRAM_LENGTH
#define CONFIG_PSRAM_LENGTH 4096 * 1024
#endif

#define CONFIG_WRAM_LENGTH 160 * 1024

/* 160K */
#define CONFIG_WRAM_ADDRESS 0x23038000 - CONFIG_WRAM_LENGTH

/* 320K + remain wram */
#define CONFIG_RAM_ADDRESS 0x62FC0000
#define CONFIG_RAM_LENGTH (320K + 160K - CONFIG_WRAM_LENGTH)

#define RAM_HOLE 0x400
#define RAM_ADDRESS CONFIG_RAM_ADDRESS + RAM_HOLE
#define RAM_LENGTH CONFIG_RAM_LENGTH - RAM_HOLE

__EM_SIZE = DEFINED(btble_controller_init) ? 32K : 0K;

#ifdef CONFIG_RISCV_PMP
#define MPU_MIN_SIZE CONFIG_PMP_GRANULARITY
#define MPU_MIN_SIZE_ALIGN . = ALIGN(MPU_MIN_SIZE);
#if defined(CONFIG_MPU_REQUIRES_POWER_OF_TWO_ALIGNMENT)
#define MPU_ALIGN(region_size) \
    . = ALIGN(MPU_MIN_SIZE); \
    . = ALIGN(1 << LOG2CEIL(region_size))
#else
#define MPU_ALIGN(region_size) \
    . = ALIGN(MPU_MIN_SIZE)
#endif
#else
#define MPU_MIN_SIZE_ALIGN
#define MPU_ALIGN(region_size) . = ALIGN(4)
#endif

#define QCC_GROUP_START(name) __##name##_start__ = ABSOLUTE(ORIGIN(name)); . = ABSOLUTE(ORIGIN(name));
#define QCC_GROUP_END(name) __##name##_end__ = ABSOLUTE(.);

#define QCC_SECTION_CODE_PROLOGUE(name, attr, n) \
    __##name##_start__ = ABSOLUTE(ADDR(name)); \
    __##name##_end__ = ABSOLUTE(ADDR(name) + SIZEOF(name)); \
    __##name##_load__ = ABSOLUTE(LOADADDR(name)); \
    name attr:
#define QCC_SECTION_DATA_PROLOGUE(name, attr, n) \
    __##name##_start__ = ABSOLUTE(ADDR(name)); \
    __##name##_end__ = ABSOLUTE(ADDR(name) + SIZEOF(name)); \
    __##name##_load__ = ABSOLUTE(LOADADDR(name)); \
    name attr:
#define QCC_SECTION_BSS_PROLOGUE(name, attr, n) \
    __##name##_start__ = ABSOLUTE(ADDR(name)); \
    __##name##_end__ = ABSOLUTE(ADDR(name) + SIZEOF(name)); \
    name attr:

#ifdef CONFIG_LPAPP
__LPFW_SIZE = 28K;
/* Link stage feature control */
PROVIDE( __macsw_conf_bl_wifi_ps_enable = 1 );
PROVIDE( __macsw_conf_plat_dma_enable = 0 );
#else
/* Link stage feature control */
PROVIDE( __macsw_conf_bl_wifi_ps_enable = 0 );
PROVIDE( __macsw_conf_plat_dma_enable = 1 );
#endif


OUTPUT_ARCH( "riscv" )
ENTRY(CONFIG_KERNEL_ENTRY)

MEMORY
{
#if CONFIG_FLASH_XIP_ADDRESS
    fw_header  (rx)   : ORIGIN = CONFIG_FLASH_XIP_ADDRESS - 0x1000, LENGTH = 4K
    rom_header (rx)   : ORIGIN = CONFIG_FLASH_XIP_ADDRESS,          LENGTH = 6K
    rom (rx)          : ORIGIN = CONFIG_FLASH_XIP_ADDRESS + 0x1800, LENGTH = CONFIG_FLASH_XIP_LENGTH

    ram_nocache (!rx) : ORIGIN = (RAM_ADDRESS) & 0x2FFFFFFF,        LENGTH = RAM_LENGTH
    ram  (!rx)        : ORIGIN = RAM_ADDRESS,                       LENGTH = RAM_LENGTH
#else
    fw_header  (rx)   : ORIGIN = RAM_ADDRESS - (256+16),            LENGTH = 256+16
    rom_header (rx)   : ORIGIN = RAM_ADDRESS,                       LENGTH = 6K

    ram_nocache (!rx) : ORIGIN = (RAM_ADDRESS + 6K) & 0x2FFFFFFF,   LENGTH = RAM_LENGTH - 6K
    ram  (!rx)        : ORIGIN = RAM_ADDRESS + 6K,                  LENGTH = RAM_LENGTH - 6K
#endif

#ifdef CONFIG_PSRAM
    ram_psram  (wxa)  : ORIGIN = CONFIG_PSRAM_ADDRESS,              LENGTH = CONFIG_PSRAM_LENGTH
#endif
    ram_wifi  (wxa)   : ORIGIN = CONFIG_WRAM_ADDRESS,               LENGTH = CONFIG_WRAM_LENGTH - __EM_SIZE
    hbn_memory (rx)   : ORIGIN = 0x20010000,                        LENGTH = 4K

#if defined(CONFIG_LINKER_DEVNULL_MEMORY)
    DEVNULL_ROM (rx)  : ORIGIN = DEVNULL_ADDR,                      LENGTH = DEVNULL_SIZE
#endif

    LINKER_DT_REGIONS()

    /* Used by and documented in include/zephyr/linker/intlist.ld */
    IDT_LIST  (wx)    : ORIGIN = 0xFFFFF7FF,                        LENGTH = 2K
}

#if !CONFIG_FLASH_XIP_ADDRESS
    REGION_ALIAS("rom", ram);
#endif

SECTIONS
{
    .fw_header :
    {
        KEEP(*(.fw_header))
#if !CONFIG_FLASH_XIP_ADDRESS
        LONG(RAM_ADDRESS)
        LONG(0x48474553)
#endif
    } > fw_header

#include <zephyr/linker/intlist.ld>
#include <zephyr/linker/rel-sections.ld>

#ifdef CONFIG_LLEXT
#include <zephyr/linker/llext-sections.ld>
#endif

    QCC_GROUP_START(rom_header)
    __rom_region_start = ORIGIN(rom_header);
    .init ORIGIN(rom_header):
    {
        KEEP (*(SORT_NONE(.init)))
        KEEP (*(SORT_NONE(.vector)))

#include <snippets-rom-start.ld>

        . = ORIGIN(rom_header) + 512;
        __multi_bins__ = ABSOLUTE(.);
        LONG(0x464C4553) LONG(0x00000000) /*SELF*/      LONG(LENGTH(fw_header)) LONG(__rom_end__ - __rom_start__ + LENGTH(fw_header) + LENGTH(rom_header))
        LONG(0x0000504C) LONG(0x00000000) /*LP*/        LONG(0x00000000) LONG(0x00000000)
        LONG(0x52415744) LONG(0x49464346) /*DWARFCFI*/  LONG(0x00000000) LONG(0x00000000)
        LONG(0x464D4F52) LONG(0x00000053) /*ROMFS*/     LONG(0x00000000) LONG(0x00000000)
        LONG(0x54524543) LONG(0x00000053) /*CERTS*/     LONG(0x00000000) LONG(0x00000000)
        LONG(0xFFFFFFFF) LONG(0xFFFFFFFF)               LONG(0xFFFFFFFF) LONG(0xFFFFFFFF)
    } > rom_header

    .rftlv.tool ORIGIN(rom_header) + 1K:
    {
        PROVIDE( _ld_symbol_rftlv_address = . );
        LONG(0x46524C42);
        LONG(0x41524150);
        . = ORIGIN(rom_header) + 1K + 2K;
    } > rom_header

    .ver ORIGIN(rom_header) + 3K:
    {
        LONG(0x42464c42);
        LONG(0x46524556);
        KEEP (*(.qcc74xverinf))
        LONG(0x42464c42);
        LONG(0x46524556);
        KEEP (*(.verinfo))
        . = ALIGN(4);
        _version_info_section_start = .;
        KEEP(*(.version.*))
        _version_info_section_end = .;
    } > rom_header

    .init_code :
    {
        /* Keep every routine used before the PSRAM copy in executable ROM. */
        *system_qcc74x*.o*(.rodata*)
        *system_qcc74x*.o*(.srodata*)
        *system_qcc74x*.o*(.text*)

        *start_load*.o*(.rodata*)
        *start_load*.o*(.srodata*)
        *start_load*.o*(.text*)

        *remain_wifi_ramrv_pmp*.o*(.rodata*)
        *rv_pmp*.o*(.srodata*)
        *rv_pmp*.o*(.text*)

        *rv_Sv39*.o*(.rodata*)
        *rv_Sv39*.o*(.srodata*)
        *rv_Sv39*.o*(.text*)

	/*
        *(.text.arch_bss_zero)
        *(.text.arch_data_copy)
        *(.text.arch_early_memset)
        *(.text.arch_early_memcpy)
        *(.text.memset)
        *(.text.memcpy)
	*/
    } > rom_header

    SECTION_PROLOGUE(reset,,)
    {
        KEEP(*(.reset.*))
    } > rom_header

    SECTION_PROLOGUE(exceptions,,)
    {
        KEEP(*(".exception.entry.*"))
        *(".exception.other.*")
    } > rom_header
    QCC_GROUP_END(rom_header)

    QCC_GROUP_START(ram_nocache)
    QCC_SECTION_DATA_PROLOGUE(ram_nocache_data, ,)
    {
        . = ALIGN(4);

        *(.nocache_ram)

        . = ALIGN(4);
    } > ram_nocache AT > rom

    QCC_SECTION_BSS_PROLOGUE(ram_nocache_noinit, (NOLOAD),)
    {
        . = ALIGN(4);
        *(.nocache_noinit_ram)
        *(.noncacheable)

        . = ALIGN(4);
    } > ram_nocache
    QCC_GROUP_END(ram_nocache)

    QCC_GROUP_START(ram)
    /* Skip the nocache region */
    .ram_skip_nocache_region (NOLOAD) :
    {
        . += (ABSOLUTE(__ram_nocache_end__) - ABSOLUTE(ORIGIN(ram_nocache)));
        . = ALIGN(32);
    } > ram

    _image_ram_start = .;
    __data_region_start = .;
    __data_start = .;

    QCC_SECTION_DATA_PROLOGUE(itcm, ,)
    {
        . = ALIGN(4);

        *(.tcm_code.*)
        *(.tcm_const.*)
        *(.sclock_rlt_code.*)
        *(.sclock_rlt_const.*)

        *qcc74x_glb*.o*(.rodata*)
        *qcc74x_pds*.o*(.rodata*)
        *qcc74x_common*.o*(.rodata*)
        *qcc74x_sf_cfg*.o*(.rodata*)
        *qcc74x_sf_ctrl*.o*(.rodata*)
        *qcc74x_sflash*.o*(.rodata*)
        *qcc74x_xip_sflash*.o*(.rodata*)
        *qcc74x_ef_ctrl*.o*(.rodata*)
        *qcc74x_ef_cfg*.o*(.rodata*)
        *qcc74x_clock*.o*(.rodata*)
        *_clz*.o*(.rodata*)
        *qcc74x_flash*.o*(.rodata*)
        *qcc74x_romapi_patch*.o*(.rodata*)

        *_udivdi3*.o*(.text*)

        PROVIDE( __global_pointer$ = . + 0x800 );

        /* ram_data has a section alignment of 8 (forced by a uint64_t
         * field inside struct bt_dev in one of its input sections). When
         * placing ram_data, ld only 8-byte-aligns the VMA and not the
         * LMA, which makes the VMA/LMA gaps differ. The single linear
         * memcpy in arch_data_copy() would then shift the entire .data
         * initializer image. Every section preceding ram_data must
         * therefore end on an ALIGN(8) boundary.
         * See docs/zephyr_sdk_beacon_no_log_debug.md §8 for details.
         */
        . = ALIGN(8);
    } > ram AT > rom

    QCC_SECTION_DATA_PROLOGUE(dtcm, ,)
    {
        . = ALIGN(4);

        *(.tcm_data)
        /* *finger_print.o(.data*) */

        /* ram_data needs ALIGN(8) on both VMA and LMA so the single linear
         * memcpy in arch_data_copy() doesn't shift the .data initializer
         * image. itcm already ends ALIGN(8); we must do the same here so
         * that any .tcm_data input (e.g. rwip_heap_non_ret) doesn't leave
         * dtcm at a non-8-aligned end, which would force ram_data VMA to
         * fill 4 bytes that the LMA doesn't have.
         * See docs/zephyr_sdk_beacon_no_log_debug.md §8.
         */
        . = ALIGN(8);
    } > ram AT > rom

    QCC_SECTION_DATA_PROLOGUE(ram_data, ,)
    {
        . = ALIGN(4);

        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        *(.sdata2)
        *(.sdata2.*)

        . = ALIGN(8);
        *(._k_queue.static.*)
        *(._k_sem.static.*)
        *(._k_mutex.static.*)

        . = ALIGN(4);
        __qcc74xlog_tags_start__ = .;
        *(.qcc74xlog_tags_array)
        . = ALIGN(4);
        __qcc74xlog_tags_end__ = .;

#include <snippets-rwdata.ld>

#ifdef CONFIG_CODE_DATA_RELOCATION
#include <linker_sram_data_relocate.ld>
#endif

        __data_end = .;
    } > ram AT > rom
    __data_size = __data_end - __data_start;
    __data_load_start = LOADADDR(itcm);
    __data_region_load_start = LOADADDR(itcm);

#include <zephyr/linker/common-ram.ld>
#include <zephyr/linker/kobject-data.ld>
#include <zephyr/linker/cplusplus-ram.ld>
#include <snippets-data-sections.ld>

    __data_region_end = .;

    QCC_SECTION_BSS_PROLOGUE(ram_bss, (NOLOAD),)
    {
        . = ALIGN(4);
        __bss_start = .;
        __kernel_ram_start = .;

        *(.bss*)
        *(.sbss*)
        COMMON_SYMBOLS

#ifdef CONFIG_CODE_DATA_RELOCATION
#include <linker_sram_bss_relocate.ld>
#endif

	__lp_mon_start = .;
        *(.lp_mon_ctx)
	__lp_mon_end = .;

        __bss_end = ALIGN(4);
    } > ram

    QCC_SECTION_BSS_PROLOGUE(ram_noinit, (NOLOAD),)
    {
        . = ALIGN(16);
        . = . + 0x1000;
        __StackTop = .;
        __freertos_irq_stack_top = .;

#ifdef CONFIG_HIGH_ISR_STACK
        . = ALIGN(16);
        . = . + 0x1000;
        __freertos_high_irq_stack_top = .;
#endif

#ifdef CONFIG_NOINIT_SNIPPET_FIRST
#include <snippets-noinit.ld>
#endif
        *(.noinit_data*)
        *(.noinit)
        *(.noinit.*)
#ifndef CONFIG_NOINIT_SNIPPET_FIRST
#include <snippets-noinit.ld>
#endif

#ifdef CONFIG_USERSPACE
        z_user_stacks_start = .;
        *(.user_stacks*)
        z_user_stacks_end = .;
#endif

        KEEP(*(.heap*))
        . = ALIGN(4);
    } > ram

#include <snippets-ram-sections.ld>

#if defined(CONFIG_USERSPACE)
#define APP_SHARED_ALIGN MPU_MIN_SIZE_ALIGN
#define SMEM_PARTITION_ALIGN MPU_ALIGN

#include <app_smem.ld>

    _app_smem_size = _app_smem_end - _app_smem_start;
    _app_smem_rom_start = LOADADDR(_APP_SMEM_SECTION_NAME);
#endif /* CONFIG_USERSPACE */

#include <zephyr/linker/kobject-priv-stacks.ld>

    __HeapBase = ABSOLUTE(.);
    __HeapLimit = ABSOLUTE(ORIGIN(ram) + LENGTH(ram));
    __kernel_ram_end = .;
    __kernel_ram_size = __kernel_ram_end - __kernel_ram_start;

/* Located in generated directory. This file is populated by the
 * zephyr_linker_sources() Cmake function.
 */
#include <snippets-sections.ld>

#define LAST_RAM_ALIGN MPU_MIN_SIZE_ALIGN
#include <zephyr/linker/ram-end.ld>

    QCC_GROUP_END(ram)

#ifdef CONFIG_PSRAM
    QCC_GROUP_START(ram_psram)

    QCC_SECTION_DATA_PROLOGUE(psram_data, ,)
    {
        . = ALIGN(32);

#ifdef CONFIG_PSRAM_COPY_CODE
        /* Relocate the library groups validated for PSRAM execution. */
        *libapp.a*:*(.text*)
        *liblib*.a*:*(.text*)
        *libdriver*.a*:*(.text*)
        *libkernel.a*:*(.text*)
        *libsubsys*.a*:*(.text*)
        *libzperf.a*:*(.text*)

        *libapp.a*:*(.rodata* .srodata*)
        *liblib*.a*:*(.rodata* .srodata*)
        *libdriver*.a*:*(.rodata* .srodata*)
        *libkernel.a*:*(.rodata* .srodata*)
        *libsubsys*.a*:*(.rodata* .srodata*)
        *libzperf.a*:*(.rodata* .srodata*)
        . = ALIGN(32);
#endif

        KEEP(*(.psram_code*))

        . = ALIGN(32);

        KEEP(*(.psram_data*))

        . = ALIGN(32);
    } > ram_psram AT > rom

    QCC_SECTION_BSS_PROLOGUE(psram_noinit_data, (NOLOAD),)
    {
        . = ALIGN(4);
        KEEP(*(.psram_noinit*))
        KEEP(*(.img_buf*))

        . = ALIGN(32);
        KEEP(*(.psram_heap*))

        . = ALIGN(4);
    } > ram_psram
    __psram_heap_base = ABSOLUTE(ALIGN(., 32));
    __psram_limit = ABSOLUTE(ORIGIN(ram_psram) + LENGTH(ram_psram));
    QCC_GROUP_END(ram_psram)
#endif

    QCC_GROUP_START(ram_wifi)
/*
    QCC_SECTION_BSS_PROLOGUE(wifi_code, ,)
    {
    } > ram_wifi AT > rom

    QCC_SECTION_BSS_PROLOGUE(wifi_data, ,)
    {
    } > ram_wifi AT > rom
*/

#ifdef CONFIG_LPAPP
    QCC_SECTION_BSS_PROLOGUE(lpfw_share, (NOLOAD),)
    {
        . = ALIGN(32);
        *(.lpfw_share_nocache_noinit*)
        __lpfw_share_used__ = ABSOLUTE(.);
        . = __LPFW_SIZE;
        . = ALIGN(32);
    } > ram_wifi
#endif

    QCC_SECTION_BSS_PROLOGUE(wifi_bss, (NOLOAD),)
    {
        _sshram = . ;
      __nocache_wifiram_bss_start__ = .;
      *(SHAREDRAMIPC)
      *(SHAREDRAM)
      *(.wifi_ram*)
      _eshram = . ;
      . = ALIGN(16);
      __nocache_wifiram_bss_end__ = .;
    } > ram_wifi
    PROVIDE( _heap_wifi_start = . );
    PROVIDE( _heap_wifi_size = ORIGIN(ram_wifi) + LENGTH(ram_wifi) - _heap_wifi_start );
    QCC_GROUP_END(ram_wifi)

    QCC_GROUP_START(hbn_memory)
    QCC_SECTION_DATA_PROLOGUE(hbn_ram, ,)
    {
        . = ALIGN(32);
        KEEP(*(.hbn_code.*))
        KEEP(*(.hbn_data.*))
        . = ALIGN(32);
    } > hbn_memory  AT > rom
    QCC_GROUP_END(hbn_memory)

    QCC_GROUP_START(rom)
    SECTION_PROLOGUE(.plt,,)
    {
        *(.plt)
    } > rom

    SECTION_PROLOGUE(.iplt,,)
    {
        *(.iplt)
    } > rom

#ifdef CONFIG_CODE_DATA_RELOCATION
#include <linker_relocate.ld>
#endif

    QCC_SECTION_BSS_PROLOGUE(rom_code, ,)
    {
        . = ALIGN(4); __mem_sections = ABSOLUTE(.);
        LONG(__ram_nocache_start__); LONG(__ram_nocache_end__); LONG(__ram_nocache_end__);
        . = ALIGN(4); __ram_section = ABSOLUTE(.);
        LONG(__ram_start__);         LONG(__ram_end__);         LONG(ORIGIN(ram)+LENGTH(ram));
        . = ALIGN(4); __psram_section = ABSOLUTE(.);
#ifdef CONFIG_PSRAM
        LONG(__ram_psram_start__);   LONG(__ram_psram_end__);   LONG(ORIGIN(ram_psram)+LENGTH(ram_psram));
#endif
        . = ALIGN(4); __ram_wifi_section = ABSOLUTE(.);
        LONG(__ram_wifi_start__);    LONG(__ram_wifi_end__);    LONG(ORIGIN(ram_wifi)+LENGTH(ram_wifi));
        LONG(0xffffffff);            LONG(0xffffffff);          LONG(0xffffffff);

        . = ALIGN(4); __mem_copy_sections = ABSOLUTE(.);
        LONG(__ram_nocache_data_start__);   LONG(__ram_nocache_data_end__);   LONG(__ram_nocache_data_load__);
        LONG(__data_region_start);          LONG(__data_region_end);          LONG(__data_region_load_start);
#ifdef CONFIG_PSRAM
        LONG(__psram_data_start__);         LONG(__psram_data_end__);         LONG(__psram_data_load__);
#endif
        LONG(0xffffffff);                   LONG(0xffffffff);                 LONG(0xffffffff);

        . = ALIGN(4); __mem_setz_sections = ABSOLUTE(.);
        LONG(__ram_bss_start__);            LONG(__ram_bss_end__);
        LONG(__wifi_bss_start__);           LONG(__wifi_bss_end__);
        LONG(0xffffffff);                   LONG(0xffffffff);

        . = ALIGN(4); __mem_noinit_sections = ABSOLUTE(.);
        LONG(__ram_nocache_noinit_start__); LONG(__ram_nocache_noinit_end__);
        LONG(__ram_noinit_start__);         LONG(__ram_noinit_end__);
#ifdef CONFIG_PSRAM
        LONG(__psram_noinit_data_start__);       LONG(__psram_noinit_data_end__);
#endif
        LONG(0xffffffff);                   LONG(0xffffffff);

        . = ALIGN(4); _dump_sections = ABSOLUTE(.);
        LONG(ORIGIN(ram_nocache));          LONG(__ram_nocache_end__ - ORIGIN(ram_nocache));
        LONG(ORIGIN(ram) + (__ram_nocache_end__ - ORIGIN(ram_nocache)));
        LONG(LENGTH(ram) - (__ram_nocache_end__ - ORIGIN(ram_nocache)));
        LONG(ORIGIN(ram_wifi));             LONG(LENGTH(ram_wifi));
#ifdef CONFIG_PSRAM
        LONG(ORIGIN(ram_psram));            LONG(LENGTH(ram_psram));
#endif
        LONG(0xffffffff);                   LONG(0xffffffff);

/*
#if defined(CONFIG_WIFI6) && !defined(CONFIG_PSRAM_COPY_CODE)
        INCLUDE macsw_cache_affinity.ld.in
#endif
*/

        KEEP(*(.openocd_debug))
        KEEP(*(".openocd_debug.*"))

        __text_region_start = .;
        *(.text*)
        *(.gnu.linkonce.t.*)
#include <zephyr/linker/kobject-text.ld>
        __text_region_end = .;

    } > rom


    __rodata_region_start = .;
#include <zephyr/linker/common-rom.ld>
#include <snippets-rom-sections.ld>
#include <zephyr/linker/thread-local-storage.ld>
#include <zephyr/linker/cplusplus-rom.ld>

    QCC_SECTION_BSS_PROLOGUE(rom_code, ,)
    {

        *(.rodata*)

        /* section information for usb usbh_class_info */
        . = ALIGN(4);
        __usbh_class_info_start__ = .;
        KEEP(*(.usbh_class_info))
        . = ALIGN(4);
        __usbh_class_info_end__ = .;

        *(.srodata*)

        /* global object for c++ */
        . = ALIGN(4);
        __preinit_array_start = .;
        KEEP (*(.preinit_array))
        __preinit_array_end = .;

        . = ALIGN(4);
        __init_array_start = .;
        KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP (*(.init_array))
        __init_array_end = .;

        . = ALIGN(4);
        *(.gcc_except_table.*)

        __gcov_info_start = .;
        KEEP (*(.gcov_info))
        KEEP (*(.gcov_info.*))
        __gcov_info_end = .;

        *(.sdata2 .sdata2.* .gnu.linkonce.s2.*)

#include <snippets-rodata.ld>
#include <zephyr/linker/kobject-rom.ld>
    } > rom
    __rodata_region_end = .;

    SECTION_PROLOGUE(.last_section,,)
    {
        KEEP(*(.last_section))
        MPU_MIN_SIZE_ALIGN
    } > rom

#ifdef CONFIG_LPAPP
    QCC_SECTION_DATA_PROLOGUE(lpfw, ,)
    {
        /* For DMA Burst copy lpfw binary from flash to ram */
        . = ALIGN(32);
        LONG(0x0);        /* LPFW SHA256 */
        . = ALIGN(32);
        LONG(0x5746504C); /* LPFW */
        . = ALIGN(32);
        __lpfw_load_addr = .;
        PROVIDE(__lpfw_start = .);
        ASSERT(((ABSOLUTE(__lpfw_start) & 0x1F) == 0),
               "LPFW not aligned 32!");
    } > rom

    PROVIDE(__binary_length = __lpfw_start - ORIGIN(rom));
    __rom_region_end = ABSOLUTE(LOADADDR(lpfw) + SIZEOF(lpfw));
#else
    __rom_region_end = ABSOLUTE(LOADADDR(.last_section) + SIZEOF(.last_section));
#endif
    __rom_region_size = __rom_region_end - __rom_region_start;
    QCC_GROUP_END(rom)

#include <zephyr/linker/debug-sections.ld>

    /DISCARD/ : { *(.note.GNU-stack) }

    SECTION_PROLOGUE(.riscv.attributes, 0,)
    {
        KEEP(*(.riscv.attributes))
        KEEP(*(.gnu.attributes))
    }

#if defined(CONFIG_LLVM_USE_LLD)
    SECTION_PROLOGUE(.symtab, 0,) { *(.symtab) }
    SECTION_PROLOGUE(.strtab, 0,) { *(.strtab) }
    SECTION_PROLOGUE(.shstrtab, 0,) { *(.shstrtab) }
#endif

    LINKER_DT_SECTIONS()

    PROVIDE(__nocache_ram_data_start__ = __ram_nocache_data_start__);
    PROVIDE(__nocache_ram_data_end__ = __ram_nocache_data_end__);
    PROVIDE(__nocache_ram_data_load_addr = __ram_nocache_data_load__);
    PROVIDE(__nocache_ram_noinit_end__ = __ram_nocache_noinit_end__);

    PROVIDE(__itcm_start = __itcm_start__);
    PROVIDE(__itcm_end = __itcm_end__);
    PROVIDE(__itcm_size = __itcm_end__ - __itcm_start__);
    PROVIDE(__itcm_load_start = __itcm_load__);

    PROVIDE(__dtcm_start = __dtcm_start__);
    PROVIDE(__dtcm_end = __dtcm_end__);
    PROVIDE(__dtcm_data_start = __dtcm_start__);
    PROVIDE(__dtcm_data_end = __dtcm_end__);
    PROVIDE(__dtcm_data_load_start = __dtcm_load__);

    PROVIDE(__psram_data_load_addr = __psram_data_load__);
    PROVIDE(__LD_CONFIG_EM_SEL = __EM_SIZE);
}
