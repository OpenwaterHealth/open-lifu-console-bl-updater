/*
 * Board support for the updater: LEDs, watchdog, flash clock, option bytes and the
 * bootloader's DFU request. Register-level, CMSIS only.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#define LED_RUN    0x1U   /* console LED_ON (PA10) */
#define LED_BUSY   0x2U   /* console HB_LED (PA1)  */

void board_leds_init(void);
void board_leds_set(uint32_t mask);
void board_iwdg_refresh(void);
void board_delay(uint32_t loops);

/* Flash erase/program on the F0 needs the HSI oscillator running. */
void board_hsi_on(void);

/* True when the option bytes show the research state the updater was made for:
 * RDP level 0 and no write protection anywhere. */
bool board_option_bytes_open(void);

/* RTC backup registers: confirm this boot (clear the boot-failure counter) and ask the
 * bootloader that runs after the next reset to enter DFU ('OWBL' + 'DFU!'). */
void board_request_bootloader_dfu(void);

#endif /* BOARD_H */
