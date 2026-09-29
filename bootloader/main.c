/* SPDX-License-Identifier: GPL-2.0-only */
#include "protocol.h"
#include "stm32g4xx.h"
#include <stddef.h>

void* memset(void* dest, int value, size_t count)
{
    uint8_t* p = dest;
    while(count--)
        *p++ = (uint8_t)value;
    return dest;
}

#define FLASH_ERRORS                                                                                             \
    (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_PGSERR | \
     FLASH_SR_MISERR | FLASH_SR_FASTERR | FLASH_SR_RDERR | FLASH_SR_OPTVERR)

extern uint32_t _estack, _sbss, _ebss;
void Reset_Handler(void);
static void fault(void)
{
    while(1) {
    }
}
static volatile bool flash_ecc_failure;
static void nmi(void)
{
    if(!(FLASH->ECCR & FLASH_ECCR_ECCD))
        fault();
    // An interrupted doubleword program may leave invalid ECC. Keep recovery reachable.
    flash_ecc_failure = true;
    FLASH->ECCR |= FLASH_ECCR_ECCD;
}
__attribute__((section(".vectors"), used)) void (*const vectors[])(void) = {(void (*)(void))&_estack,
                                                                            Reset_Handler,
                                                                            nmi,
                                                                            fault,
                                                                            fault,
                                                                            fault,
                                                                            fault,
                                                                            0,
                                                                            0,
                                                                            0,
                                                                            0,
                                                                            fault,
                                                                            fault,
                                                                            0,
                                                                            fault,
                                                                            fault};

static uint32_t expected_size, expected_crc, written;
static bool updating, session;
static opg4_parser_t parser;

static bool expired(uint32_t start, uint32_t ms)
{
    return (uint32_t)(DWT->CYCCNT - start) >= ms * 16000U;
}
static void watchdog(void)
{
    IWDG->KR = 0xAAAAU;
}

static bool device_valid(void)
{
    return (DBGMCU->IDCODE & 0xFFFU) == 0x468U && *(const uint16_t*)FLASHSIZE_BASE == 128U;
}

static bool application_valid(void)
{
    const uint32_t* metadata = (const uint32_t*)OPG4_METADATA;
    const uint32_t* app      = (const uint32_t*)OPG4_APP_START;
    return metadata[0] == OPG4_COMMITTED && metadata[1] == ~OPG4_COMMITTED &&
           opg4_vectors_valid(app[0], app[1], OPG4_APP_END - OPG4_APP_START) && !flash_ecc_failure;
}

static bool flash_wait(void)
{
    uint32_t start = DWT->CYCCNT;
    while(FLASH->SR & FLASH_SR_BSY) {
        watchdog();
        if(expired(start, 1000))
            return false;
    }
    return !(FLASH->SR & FLASH_ERRORS);
}

static bool flash_unlock(void)
{
    if(!flash_wait())
        FLASH->SR = FLASH_ERRORS | FLASH_SR_EOP;
    if(FLASH->SR & FLASH_SR_BSY)
        return false;
    if(FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123U;
        FLASH->KEYR = 0xCDEF89ABU;
    }
    FLASH->SR = FLASH_ERRORS | FLASH_SR_EOP;
    return !(FLASH->CR & FLASH_CR_LOCK);
}

static bool erase_page(uint32_t address)
{
    if(address < OPG4_METADATA || address >= OPG4_APP_END || address % OPG4_PAGE_SIZE)
        return false;
    if(!flash_unlock())
        return false;
    FLASH->CR = ((address - FLASH_BASE) / OPG4_PAGE_SIZE) << FLASH_CR_PNB_Pos | FLASH_CR_PER;
    FLASH->CR |= FLASH_CR_STRT;
    bool ok   = flash_wait();
    FLASH->CR = FLASH_CR_LOCK;
    return ok;
}

static bool program_doubleword(uint32_t address, uint32_t low, uint32_t high)
{
    if(address < OPG4_METADATA || address > OPG4_APP_END - 8 || address % 8)
        return false;
    if(!flash_unlock())
        return false;
    FLASH->CR                    = FLASH_CR_PG;
    *(volatile uint32_t*)address = low;
    __ISB();
    *(volatile uint32_t*)(address + 4) = high;
    bool ok                            = flash_wait();
    FLASH->CR                          = FLASH_CR_LOCK;
    return ok && *(const uint32_t*)address == low && *(const uint32_t*)(address + 4) == high && !flash_ecc_failure;
}

static void send_response(uint8_t command, uint16_t sequence, uint8_t status, const uint8_t* data, uint16_t size)
{
    uint8_t frame[40] = {'O', 'P', 'B', 'R', command | 0x80U, sequence, sequence >> 8, size + 1, 0, status};
    for(unsigned i = 0; i < size; i++)
        frame[10 + i] = data[i];
    opg4_put32(frame + 10 + size, opg4_crc(frame + 4, 6 + size));
    for(unsigned i = 0; i < 14U + size; i++) {
        while(!(USART1->ISR & USART_ISR_TXE))
            watchdog();
        USART1->TDR = frame[i];
    }
    while(!(USART1->ISR & USART_ISR_TC))
        watchdog();
}

static void command(void)
{
    uint8_t cmd  = parser.bytes[4];
    uint16_t seq = opg4_u16(parser.bytes + 5), size = opg4_u16(parser.bytes + 7);
    const uint8_t* data = parser.bytes + 9;
    uint8_t status      = OPG4_OK;
    if(!device_valid()) {
        send_response(cmd, seq, OPG4_BAD_DEVICE, 0, 0);
        return;
    }
    switch(cmd) {
    case OPG4_INFO: {
        if(size) {
            status = OPG4_BAD_COMMAND;
            break;
        }
        session          = true;
        uint8_t info[24] = {'O', 'P', 'G', '4', 1, 0, 0x68, 0x04};
        opg4_put32(info + 8, OPG4_APP_START);
        opg4_put32(info + 12, OPG4_APP_END);
        opg4_put32(info + 16, OPG4_PAGE_SIZE);
        info[20] = 0;
        info[21] = 1;
        info[22] = 128;
        info[23] = 0;
        send_response(cmd, seq, OPG4_OK, info, sizeof(info));
        return;
    }
    case OPG4_BEGIN:
        if(!session) {
            status = OPG4_BAD_STATE;
            break;
        }
        if(size != 8 || opg4_u32(data) < 8 || opg4_u32(data) > OPG4_APP_END - OPG4_APP_START || opg4_u32(data) % 8) {
            status = OPG4_BAD_RANGE;
            break;
        }
        updating      = false;
        expected_size = opg4_u32(data);
        expected_crc  = opg4_u32(data + 4);
        written       = 0;
        // Invalidate first: power loss must never boot a partially replaced application.
        if(!erase_page(OPG4_METADATA)) {
            status = OPG4_FLASH_ERROR;
            break;
        }
        for(uint32_t address = OPG4_APP_START; address < OPG4_APP_START + expected_size; address += OPG4_PAGE_SIZE) {
            watchdog();
            if(!erase_page(address)) {
                status = OPG4_FLASH_ERROR;
                break;
            }
        }
        updating = status == OPG4_OK;
        if(updating)
            flash_ecc_failure = false;
        break;
    case OPG4_WRITE: {
        if(!updating) {
            status = OPG4_BAD_STATE;
            break;
        }
        if(size < 12 || size > OPG4_MAX_PAYLOAD || (size - 4) % 8 || opg4_u32(data) != written ||
           size - 4U > expected_size - written) {
            status = OPG4_BAD_RANGE;
            break;
        }
        for(unsigned i = 4; i < size; i += 8) {
            if(!program_doubleword(OPG4_APP_START + written, opg4_u32(data + i), opg4_u32(data + i + 4))) {
                updating = false;
                status   = OPG4_FLASH_ERROR;
                break;
            }
            written += 8;
        }
        break;
    }
    case OPG4_COMMIT: {
        if(size || !updating || written != expected_size) {
            status = OPG4_BAD_STATE;
            break;
        }
        updating            = false;
        const uint32_t* app = (const uint32_t*)OPG4_APP_START;
        if(!opg4_vectors_valid(app[0], app[1], expected_size) ||
           opg4_crc((const uint8_t*)app, expected_size) != expected_crc || flash_ecc_failure) {
            status = OPG4_BAD_IMAGE;
            break;
        }
        if(!program_doubleword(OPG4_METADATA, OPG4_COMMITTED, ~OPG4_COMMITTED))
            status = OPG4_FLASH_ERROR;
        break;
    }
    case OPG4_RUN:
        if(size || updating || !session || !application_valid()) {
            status = OPG4_BAD_STATE;
            break;
        }
        send_response(cmd, seq, OPG4_OK, 0, 0);
        NVIC_SystemReset();
        break;
    case OPG4_ENTER:
        // ENTER is also harmless when the application is already in recovery mode.
        return;
    default:
        status = OPG4_BAD_COMMAND;
    }
    send_response(cmd, seq, status, 0, 0);
}

__attribute__((naked, noreturn)) static void jump(uint32_t stack __attribute__((unused)),
                                                  uint32_t entry __attribute__((unused)))
{
    __asm volatile("msr msp, r0\n bx r1");
}

void Reset_Handler(void)
{
    for(uint32_t* p = &_sbss; p < &_ebss;)
        *p++ = 0;
    SCB->VTOR = FLASH_BASE;
    // No cache is enabled while flash is erased/programmed/read back.
    FLASH->ACR &= ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN);
    FLASH->ACR |= FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    FLASH->ACR &= ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN | RCC_APB1ENR1_RTCAPBEN;
    (void)RCC->APB1ENR1;
    PWR->CR1 |= PWR_CR1_DBP;
    while(!(PWR->CR1 & PWR_CR1_DBP)) {
    }
    bool stay   = TAMP->BKP2R == OPG4_BOOT_REQUEST || !application_valid();
    TAMP->BKP2R = 0;
    PWR->CR1 &= ~PWR_CR1_DBP;
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    GPIOA->MODER   = (GPIOA->MODER & ~((3U << 18) | (3U << 20))) | (2U << 18) | (2U << 20);
    GPIOA->AFR[1]  = (GPIOA->AFR[1] & ~0xFF0U) | 0x770U;
    GPIOA->PUPDR   = (GPIOA->PUPDR & ~(3U << 20)) | (1U << 20);
    USART1->BRR    = 139;  // HSI16 / 115200, oversampling 16, 8N1.
    USART1->CR1    = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    uint32_t start = DWT->CYCCNT, last = start;
    while(1) {
        watchdog();
        if(!stay && !session && expired(start, 1500)) {
            USART1->CR1 = 0;
            RCC->APB2RSTR |= RCC_APB2RSTR_USART1RST;
            RCC->APB2RSTR &= ~RCC_APB2RSTR_USART1RST;
            SCB->VTOR = OPG4_APP_START;
            __DSB();
            __ISB();
            jump(*(const uint32_t*)OPG4_APP_START, *(const uint32_t*)(OPG4_APP_START + 4));
        }
        if(USART1->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) {
            USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
            parser.used = 0;
        }
        if(USART1->ISR & USART_ISR_RXNE) {
            if(expired(last, 100))
                parser.used = 0;
            last = DWT->CYCCNT;
            if(opg4_receive(&parser, USART1->RDR))
                command();
        }
    }
}
