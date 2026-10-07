/*
 * The boot state: an append-only log of 32-byte records (one flash word
 * each) in sector 7. The newest valid record wins. Appending never rewrites
 * an existing record, so losing power while appending leaves the previous
 * record in force: either the new record is complete and its CRC is right,
 * or it is ignored.
 */
#ifndef BOOT_STATE_H
#define BOOT_STATE_H

#include <stdbool.h>
#include <stdint.h>

#define BOOT_RECORD_MAGIC 0xB0075747u

enum boot_slot_state {
    SLOT_CONFIRMED = 0xC0FFEE01u, /* known good: boot it */
    SLOT_PENDING = 0x7E57AB1Eu,   /* new image on trial: must confirm itself */
};

struct boot_record {
    uint32_t magic;
    uint32_t seq;         /* increases by one per record */
    uint32_t slot;        /* the active slot */
    uint32_t state;       /* enum boot_slot_state */
    uint32_t attempts;    /* boots of a PENDING image so far */
    uint32_t min_counter; /* anti-rollback: lowest acceptable security_counter */
    uint32_t reserved;
    uint32_t crc;         /* CRC-32 of the 28 bytes above */
};

_Static_assert(sizeof(struct boot_record) == 32, "one flash word");

/* Newest valid record, or false if there is none (first boot, or the log was lost). */
bool boot_state_read(struct boot_record *out);

/* Appends r (seq and crc are filled in). Erases and restarts the log when the sector is full. */
bool boot_state_append(struct boot_record *r);

uint32_t crc32(const void *data, uint32_t len);

#endif
