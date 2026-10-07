/*
 * Wire format from the logger to the host (tools/lab4_rx.py):
 *
 *   0xA5 | type | seq (u16 LE) | len (u16 LE) | payload[len] | crc16 (u16 LE)
 *
 * type 0x01: payload = len/2 filtered samples (int16 LE)
 * type 0x02: payload = ASCII status text
 * seq counts sample frames only; a gap on the host means data was lost.
 * crc16 is CRC-16/CCITT-FALSE over everything before it.
 */
#ifndef FRAME_H
#define FRAME_H

#include <stddef.h>
#include <stdint.h>

#define FRAME_SYNC 0xA5u
#define FRAME_SAMPLES 0x01u
#define FRAME_TEXT 0x02u
#define FRAME_HDR_SIZE 6u
#define FRAME_CRC_SIZE 2u

uint16_t crc16_ccitt(const uint8_t *data, size_t len);

/* Builds a frame in buf (room for FRAME_HDR_SIZE + len + FRAME_CRC_SIZE).
 * Returns the total frame size. */
size_t frame_build(uint8_t *buf, uint8_t type, uint16_t seq, const void *payload, uint16_t len);

#endif
