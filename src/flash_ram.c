/*
 * RAM-resident bootloader rewrite for the STM32F072 (2 KB pages, half-word
 * programming). See flash_ram.h.
 *
 * Why RAM: the core stalls on any flash fetch while the flash controller is busy, and
 * the bootloader armed the independent watchdog before it launched us. Running the
 * wait loops from SRAM keeps the watchdog fed, and lets the last routine erase the
 * slot this image runs from.
 *
 * Compiled with -fno-builtin -fno-tree-loop-distribute-patterns -fno-jump-tables so
 * the loops cannot become memcpy/memset calls or table jumps into flash; checked at
 * build time by scripts/check_ramfunc.sh.
 */
#include "flash_ram.h"
#include "stm32f0xx.h"
#include "updater_layout.h"

#define RAMFUNC __attribute__((section(".RamFunc"), noinline, used))

#define UNLOCK_KEY1      0x45670123UL
#define UNLOCK_KEY2      0xCDEF89ABUL
#define SR_ERRORS       (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)
#define SR_CLEAR        (FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPRTERR)
#define IWDG_RELOAD_KEY 0x0000AAAAUL
/* Finite bound on every busy wait: far beyond the datasheet page-erase time (40 ms)
 * at any core clock the bootloader may have left, but not forever. */
#define WAIT_LIMIT      0x01000000UL

RAMFUNC static int ram_wait_idle(void)
{
    uint32_t n = 0U;
    while ((FLASH->SR & FLASH_SR_BSY) != 0U) {
        IWDG->KR = IWDG_RELOAD_KEY;
        if (++n > WAIT_LIMIT) {
            return 0;
        }
    }
    return 1;
}

RAMFUNC static void ram_unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0U) {
        FLASH->KEYR = UNLOCK_KEY1;
        FLASH->KEYR = UNLOCK_KEY2;
    }
    FLASH->SR = SR_CLEAR;
}

RAMFUNC static int ram_erase_page(uint32_t page)
{
    int ok;
    (void)ram_wait_idle();
    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = FLASH_BASE_ADDR + (page * FLASH_PAGE_SIZE);
    FLASH->CR |= FLASH_CR_STRT;
    ok = ram_wait_idle();
    FLASH->CR &= ~FLASH_CR_PER;
    if ((FLASH->SR & SR_ERRORS) != 0U) {
        ok = 0;
    }
    FLASH->SR = SR_CLEAR;
    return ok;
}

RAMFUNC flash_ram_result_t flash_ram_rewrite_bootloader(const uint8_t *src, uint32_t size,
                                                        flash_ram_stats_t *stats)
{
    volatile uint16_t *dst16 = (volatile uint16_t *)FLASH_BASE_ADDR;
    const volatile uint8_t *dst8 = (const volatile uint8_t *)FLASH_BASE_ADDR;
    uint32_t halfwords = (size + 1U) / 2U;

    stats->status = 0U;
    stats->error_offset = 0U;
    ram_unlock();

    /* ---- erase the bootloader pages ---- */
    for (uint32_t page = BL_FIRST_PAGE; page <= BL_LAST_PAGE; page++) {
        if (!ram_erase_page(page)) {
            stats->status = FLASH->SR;
            stats->error_offset = page * FLASH_PAGE_SIZE;
            FLASH->CR |= FLASH_CR_LOCK;
            return FLASH_RAM_ERASE_FAILED;
        }
    }

    /* ---- program, one half-word at a time (odd tail padded with 0xFF) ---- */
    FLASH->CR |= FLASH_CR_PG;
    for (uint32_t i = 0U; i < halfwords; i++) {
        uint32_t b = i * 2U;
        uint16_t lo = src[b];
        uint16_t hi = (b + 1U < size) ? src[b + 1U] : 0xFFU;
        IWDG->KR = IWDG_RELOAD_KEY;
        dst16[i] = (uint16_t)(lo | (uint16_t)(hi << 8));
        if (!ram_wait_idle() || (FLASH->SR & SR_ERRORS) != 0U) {
            stats->status = FLASH->SR;
            stats->error_offset = b;
            FLASH->SR = SR_CLEAR;
            FLASH->CR &= ~FLASH_CR_PG;
            FLASH->CR |= FLASH_CR_LOCK;
            return FLASH_RAM_PROGRAM_FAILED;
        }
        FLASH->SR = SR_CLEAR;
    }
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;

    /* ---- verify by read-back; the rest of the region must read erased ---- */
    for (uint32_t i = 0U; i < BL_REGION_SIZE; i++) {
        uint8_t want = (i < size) ? src[i] : 0xFFU;
        if ((i & 0x3FFU) == 0U) {
            IWDG->KR = IWDG_RELOAD_KEY;
        }
        if (dst8[i] != want) {
            stats->error_offset = i;
            return FLASH_RAM_VERIFY_MISMATCH;
        }
    }
    return FLASH_RAM_OK;
}

RAMFUNC void flash_ram_erase_pages_and_reset(uint32_t first, uint32_t last, uint32_t keep)
{
    ram_unlock();
    for (uint32_t page = first; page <= last; page++) {
        if (page != keep) {
            (void)ram_erase_page(page);   /* bounded; on failure we reset anyway */
        }
    }
    FLASH->CR |= FLASH_CR_LOCK;
    __DSB();
    SCB->AIRCR = (0x5FAUL << SCB_AIRCR_VECTKEY_Pos) | SCB_AIRCR_SYSRESETREQ_Msk;
    __DSB();
    for (;;) {
        IWDG->KR = IWDG_RELOAD_KEY;
    }
}
