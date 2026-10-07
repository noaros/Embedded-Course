#include "boot_state.h"

#include <stddef.h>
#include <string.h>

#include "flash_h7.h"
#include "flash_layout.h"

#define MAX_RECORDS (STATE_SIZE / sizeof(struct boot_record))

uint32_t crc32(const void *data, uint32_t len)
{
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        crc ^= *p++;
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static const struct boot_record *slot_record(uint32_t i)
{
    return (const struct boot_record *)(STATE_BASE + i * sizeof(struct boot_record));
}

static bool is_erased(const struct boot_record *r)
{
    const uint32_t *w = (const uint32_t *)r;
    for (int i = 0; i < 8; i++) {
        if (w[i] != 0xFFFFFFFFu) {
            return false;
        }
    }
    return true;
}

static bool is_valid(const struct boot_record *r)
{
    return r->magic == BOOT_RECORD_MAGIC && r->crc == crc32(r, offsetof(struct boot_record, crc));
}

/* Index of the first erased record, or MAX_RECORDS if the sector is full. */
static uint32_t first_free(void)
{
    uint32_t i = 0;
    while (i < MAX_RECORDS && !is_erased(slot_record(i))) {
        i++;
    }
    return i;
}

bool boot_state_read(struct boot_record *out)
{
    bool found = false;
    uint32_t end = first_free();
    for (uint32_t i = 0; i < end; i++) {
        const struct boot_record *r = slot_record(i);
        if (is_valid(r) && (!found || (int32_t)(r->seq - out->seq) > 0)) {
            *out = *r;
            found = true;
        }
    }
    return found;
}

bool boot_state_append(struct boot_record *r)
{
    struct boot_record last;
    r->magic = BOOT_RECORD_MAGIC;
    r->seq = boot_state_read(&last) ? last.seq + 1u : 1u;
    r->reserved = 0xFFFFFFFFu;
    r->crc = crc32(r, offsetof(struct boot_record, crc));

    uint32_t i = first_free();
    if (i == MAX_RECORDS) {
        /* Known weakness: a power cut between this erase and the program
         * below loses the log. The bootloader then falls back to the best
         * valid image. A second sector used ping-pong would close this gap
         * (a stretch goal in the lab). */
        if (!flash_erase_sector(STATE_SECTOR)) {
            return false;
        }
        i = 0;
    }
    uint32_t words[8];
    memcpy(words, r, sizeof words);
    return flash_program_word(STATE_BASE + i * sizeof(struct boot_record), words);
}
