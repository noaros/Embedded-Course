/*
 * Signed image header. It occupies the first 1 KB of a slot; the
 * application's vector table follows at slot + 0x400 (1 KB aligned, as VTOR
 * requires for 179 vectors).
 *
 * signature = Ed25519( SHA-512( header[0 .. offsetof(signature)) || payload ) )
 * made by tools/fw_sign.py.
 */
#ifndef IMAGE_H
#define IMAGE_H

#include <stddef.h>
#include <stdint.h>

#define IMAGE_MAGIC       0x31474D49u /* "IMG1" */
#define IMAGE_HEADER_SIZE 0x400u

struct image_header {
    uint32_t magic;
    uint32_t header_size;      /* IMAGE_HEADER_SIZE */
    uint32_t payload_size;     /* bytes after the header */
    uint32_t version;          /* major << 16 | minor << 8 | patch */
    uint32_t security_counter; /* anti-rollback: must be >= the stored minimum */
    uint32_t slot;             /* SLOT_A or SLOT_B: the slot this build was linked for */
    uint8_t reserved[IMAGE_HEADER_SIZE - 6 * 4 - 64];
    uint8_t signature[64];
};

_Static_assert(sizeof(struct image_header) == IMAGE_HEADER_SIZE, "header size");
_Static_assert(offsetof(struct image_header, signature) == IMAGE_HEADER_SIZE - 64, "sig offset");

#endif
