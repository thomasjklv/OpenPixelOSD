/* SPDX-License-Identifier: GPL-2.0-only */
#include "protocol.h"

uint32_t opg4_crc(const uint8_t* data, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFU;
    while(size--) {
        crc ^= *data++;
        for(unsigned i = 0; i < 8; i++)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return ~crc;
}

bool opg4_receive(opg4_parser_t* parser, uint8_t byte)
{
    static const uint8_t magic[] = {'O', 'P', 'B', 'L'};
    if(parser->used < 4 && byte != magic[parser->used]) {
        parser->used     = byte == magic[0] ? 1 : 0;
        parser->bytes[0] = magic[0];
        return false;
    }
    parser->bytes[parser->used++] = byte;
    if(parser->used < 9)
        return false;
    uint16_t size = opg4_u16(parser->bytes + 7);
    if(size > OPG4_MAX_PAYLOAD) {
        parser->used = 0;
        return false;
    }
    if(parser->used != 13 + size)
        return false;
    parser->used = 0;
    return opg4_crc(parser->bytes + 4, 5 + size) == opg4_u32(parser->bytes + 9 + size);
}

bool opg4_vectors_valid(uint32_t stack, uint32_t entry, uint32_t length)
{
    bool ram = (stack > 0x20000000U && stack <= 0x20005800U) || (stack > 0x10000000U && stack <= 0x10002800U);
    return length >= 8 && length <= OPG4_APP_END - OPG4_APP_START && ram && !(stack & 7U) && (entry & 1U) &&
           entry >= OPG4_APP_START && entry < OPG4_CODE_END && entry < OPG4_APP_START + length;
}
