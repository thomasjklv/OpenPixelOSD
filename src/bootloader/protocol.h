/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OPG4_PROTOCOL_H
#define OPG4_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>

#define OPG4_APP_START    0x08002000U
#define OPG4_CODE_END     0x08019000U
#define OPG4_APP_END      0x0801F000U
#define OPG4_METADATA     0x08001800U
#define OPG4_PAGE_SIZE    2048U
#define OPG4_MAX_DATA     256U
#define OPG4_MAX_PAYLOAD  (OPG4_MAX_DATA + 4U)
#define OPG4_BOOT_REQUEST 0x3447504FU
#define OPG4_COMMITTED    0x3147504FU

enum {
    OPG4_INFO = 1,
    OPG4_BEGIN,
    OPG4_WRITE,
    OPG4_COMMIT,
    OPG4_RUN,
    OPG4_ENTER = 16
};
enum {
    OPG4_OK = 0,
    OPG4_BAD_COMMAND,
    OPG4_BAD_RANGE,
    OPG4_BAD_STATE,
    OPG4_FLASH_ERROR,
    OPG4_BAD_IMAGE,
    OPG4_BAD_DEVICE
};

typedef struct {
    uint8_t bytes[9 + OPG4_MAX_PAYLOAD + 4];
    uint16_t used;
} opg4_parser_t;

static inline uint16_t opg4_u16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline uint32_t opg4_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void opg4_put32(uint8_t* p, uint32_t value)
{
    for(unsigned i = 0; i < 4; i++)
        p[i] = value >> (8 * i);
}

uint32_t opg4_crc(const uint8_t* data, uint32_t size);
bool opg4_receive(opg4_parser_t* parser, uint8_t byte);
bool opg4_vectors_valid(uint32_t stack, uint32_t entry, uint32_t length);
void opg4_enter_byte(uint8_t byte);
#endif
