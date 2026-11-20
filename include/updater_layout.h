/*
 * Flash layout the updater works with: Open-LIFU console, STM32F072RB
 * (128 KB flash, 64 pages of 2 KB, half-word programming).
 *
 *   page  0-31  0x08000000  64K  secure bootloader (rewritten by this updater)
 *   page 32-60  0x08010000  58K  application slot (this updater runs here, at +0x400)
 *   page 61     0x0801E800   2K  presets (bootloader >= 1.1.0); tail of the slot before
 *   page 62     0x0801F000   2K  LIFU runtime config (serial, calibration): KEPT
 *   page 63     0x0801F800   2K  anti-rollback floor (both bootloaders, same format)
 *
 * Bootloader 1.0.3-rc.1 (fielded with app 1.2.6) has a 60K slot, pages 32-61; 1.1.0
 * moves page 61 to the presets. The updater erases pages 32-61 and 63 before it resets,
 * so the new bootloader starts with an empty slot, a blank preset page and no floor.
 */
#ifndef UPDATER_LAYOUT_H
#define UPDATER_LAYOUT_H

#define FLASH_BASE_ADDR      0x08000000UL
#define FLASH_PAGE_SIZE      0x800UL
#define FLASH_PAGE_COUNT     64UL

#define BL_FIRST_PAGE        0UL
#define BL_LAST_PAGE         31UL
#define BL_REGION_SIZE       ((BL_LAST_PAGE - BL_FIRST_PAGE + 1UL) * FLASH_PAGE_SIZE)  /* 64 KB */

/* Pages erased after the bootloader is in place: the old slot, the new preset page and
 * the anti-rollback floor. The runtime-config page is kept. */
#define CLEAN_FIRST_PAGE     32UL
#define CLEAN_LAST_PAGE      63UL
#define CLEAN_KEEP_PAGE      62UL

/* The embedded bootloader's info block (bootloader >= 1.1.0): 'OWBI' at this offset. */
#define BL_INFO_OFFSET       0xE0UL
#define BL_INFO_MAGIC        0x4942574FUL     /* "OWBI" little-endian */

/* SRAM: 16 KB at 0x20000000. */
#define SRAM_BASE_ADDR       0x20000000UL
#define SRAM_END_ADDR        0x20004000UL

#endif /* UPDATER_LAYOUT_H */
