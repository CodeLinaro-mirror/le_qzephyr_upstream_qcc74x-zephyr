#include <zephyr/kernel.h>

/* Emit an explicit zero-filled header so imgtool/wrapper can sign the
 * TF-M non-secure image without adding --pad-header itself.
 */

#ifndef CONFIG_ROM_START_OFFSET
#error "CONFIG_ROM_START_OFFSET must be defined by Kconfig"
#endif

__attribute__((section(".mcuboot_header")))
const unsigned char __mcuboot_header[CONFIG_ROM_START_OFFSET] = {0};
