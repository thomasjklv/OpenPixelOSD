/* SPDX-License-Identifier: GPL-2.0-only */
#include "protocol.h"
#include "stm32g4xx.h"
#include "stm32g4xx_hal.h"
#include <string.h>

void opg4_enter_byte(uint8_t byte)
{
    static opg4_parser_t parser;
    static uint32_t last;
    uint32_t now = HAL_GetTick();
    if(now - last > 100)
        parser.used = 0;
    last = now;
    if(!opg4_receive(&parser, byte) || parser.bytes[4] != OPG4_ENTER || opg4_u16(parser.bytes + 7) != 8 ||
       memcmp(parser.bytes + 9, "BOOTG431", 8))
        return;
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    (void)RCC->APB1ENR1;
    PWR->CR1 |= PWR_CR1_DBP;
    while(!(PWR->CR1 & PWR_CR1_DBP)) {
    }
    RCC->APB1ENR1 |= RCC_APB1ENR1_RTCAPBEN;
    (void)RCC->APB1ENR1;
    TAMP->BKP2R = OPG4_BOOT_REQUEST;
    __DSB();
    NVIC_SystemReset();
}
