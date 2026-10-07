#include "frame.h"

#include <string.h>

uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (uint16_t)(crc ^ ((unsigned)data[i] << 8));
        for (int b = 0; b < 8; b++) {
            unsigned shifted = (unsigned)crc << 1;
            crc = (uint16_t)((crc & 0x8000u) ? (shifted ^ 0x1021u) : shifted);
        }
    }
    return crc;
}

size_t frame_build(uint8_t *buf, uint8_t type, uint16_t seq, const void *payload, uint16_t len)
{
    buf[0] = FRAME_SYNC;
    buf[1] = type;
    buf[2] = (uint8_t)seq;
    buf[3] = (uint8_t)(seq >> 8);
    buf[4] = (uint8_t)len;
    buf[5] = (uint8_t)(len >> 8);
    memcpy(buf + FRAME_HDR_SIZE, payload, len);
    uint16_t crc = crc16_ccitt(buf, FRAME_HDR_SIZE + len);
    buf[FRAME_HDR_SIZE + len] = (uint8_t)crc;
    buf[FRAME_HDR_SIZE + len + 1] = (uint8_t)(crc >> 8);
    return FRAME_HDR_SIZE + len + FRAME_CRC_SIZE;
}
