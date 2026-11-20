/*
 * Startup for the updater (Cortex-M0, STM32F072): vector table and Reset_Handler.
 *
 * No C library start-up: copy .data (which holds the RAM-resident flash code,
 * .RamFunc), zero .bss, call main(). The secure bootloader has already set up the
 * clocks; it copies this vector table to SRAM at 0x20000000 and remaps SRAM to address
 * 0 before the jump (the M0 has no VTOR), which is why RAM starts at 0x200000C0 in the
 * linker script. Interrupts stay masked for the whole run; every exception ends in the
 * FAIL pattern rather than a silent lock-up.
 */
#include <stdint.h>

#include "board.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);

void Reset_Handler(void) __attribute__((noreturn));
void Default_Handler(void) __attribute__((noreturn));

void Reset_Handler(void)
{
    const uint32_t *src = &_sidata;
    /* The bounds are linker symbols: one region each, so comparing them is defined. */
    // cppcheck-suppress comparePointers
    for (uint32_t *dst = &_sdata; dst < &_edata; ) {
        *dst++ = *src++;
    }
    // cppcheck-suppress comparePointers
    for (uint32_t *dst = &_sbss; dst < &_ebss; ) {
        *dst++ = 0U;
    }
    (void)main();
    for (;;) { }
}

void Default_Handler(void)
{
    for (;;) {
        board_iwdg_refresh();
        board_leds_set(LED_RUN | LED_BUSY);
    }
}

/* 16 core entries + 32 STM32F072 interrupts (48 words, 0xC0 bytes: what the bootloader
 * copies to SRAM). */
#define VECTOR_COUNT 48U
typedef void (*vector_t)(void);

__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[VECTOR_COUNT] = {
    (vector_t)(uintptr_t)&_estack,
    Reset_Handler,
    [2 ... VECTOR_COUNT - 1U] = Default_Handler,
};
