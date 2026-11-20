/*
 * Board support, Open-LIFU console (STM32F072RB). See board.h.
 */
#include "board.h"
#include "stm32f0xx.h"

#define LED_RUN_PIN   10U   /* PA10, LED_ON */
#define LED_BUSY_PIN  1U    /* PA1,  HB_LED */

/* Contract with open-lifu-console-bl (1.0.x and 1.1.x), RTC backup registers. */
#define BL_BKP_SIGNATURE      0x4F57424CUL   /* BKP0R 'OWBL' */
#define BL_BKP_REQ_DFU_MAGIC  0x21554644UL   /* BKP1R 'DFU!' */

static void pin_output(uint32_t pin)
{
    /* Touch only this pin's mode bits: the bootloader configured others (USB). */
    GPIOA->MODER = (GPIOA->MODER & ~(3UL << (pin * 2U))) | (1UL << (pin * 2U));
    GPIOA->OTYPER &= ~(1UL << pin);
}

void board_leds_init(void)
{
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    (void)RCC->AHBENR;
    pin_output(LED_RUN_PIN);
    pin_output(LED_BUSY_PIN);
    board_leds_set(0U);
}

void board_leds_set(uint32_t mask)
{
    uint32_t set = 0U, reset = 0U;
    if ((mask & LED_RUN) != 0U)  { set |= 1UL << LED_RUN_PIN; }  else { reset |= 1UL << LED_RUN_PIN; }
    if ((mask & LED_BUSY) != 0U) { set |= 1UL << LED_BUSY_PIN; } else { reset |= 1UL << LED_BUSY_PIN; }
    GPIOA->BSRR = set | (reset << 16U);
}

void board_iwdg_refresh(void)
{
    IWDG->KR = 0x0000AAAAUL;
}

void board_delay(uint32_t loops)
{
    for (volatile uint32_t i = 0U; i < loops; i++) {
        if ((i & 0xFFFU) == 0U) {
            board_iwdg_refresh();
        }
    }
}

void board_hsi_on(void)
{
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0U) {
        board_iwdg_refresh();
    }
}

bool board_option_bytes_open(void)
{
    /* OBR.RDPRT: 00 = level 0. WRPR: a 0 bit write-protects its pages. */
    bool rdp0 = (FLASH->OBR & (FLASH_OBR_RDPRT1 | FLASH_OBR_RDPRT2)) == 0U;
    bool no_wrp = FLASH->WRPR == 0xFFFFFFFFUL;
    return rdp0 && no_wrp;
}

void board_request_bootloader_dfu(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR |= PWR_CR_DBP;
    if ((RCC->BDCR & RCC_BDCR_RTCEN) == 0U) {
        if ((RCC->BDCR & RCC_BDCR_RTCSEL) == 0U) {
            RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL) | RCC_BDCR_RTCSEL_1;   /* LSI */
        }
        RCC->BDCR |= RCC_BDCR_RTCEN;
    }
    RTC->BKP0R = BL_BKP_SIGNATURE;
    RTC->BKP2R = 0U;                 /* boot-failure counter and flags */
    RTC->BKP3R = 0U;                 /* last-bad-image marker */
    RTC->BKP1R = BL_BKP_REQ_DFU_MAGIC;
    __DSB();
}
