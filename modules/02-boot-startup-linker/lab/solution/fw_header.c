/*
 * Stretch goal: a firmware header at a fixed flash address (0x08000400),
 * which a bootloader or a production tester can read without parsing ELF.
 */
#include <stdint.h>

#define FW_HEADER_MAGIC 0x46574844u /* "DHWF" in memory, "FWHD" when read as a word */

struct fw_header {
    uint32_t magic;
    uint32_t header_version;
    uint32_t version;            /* major << 16 | minor << 8 | patch */
    uint32_t image_size;         /* bytes from the start of flash to the end of the image */
    const void *build_id_note;   /* the ELF note: 16-byte header, then 20 bytes of SHA-1 */
};

/* Defined by stm32h723.ld. Only their addresses are meaningful. */
extern uint8_t _image_size[];
extern uint8_t __build_id_note[];

__attribute__((section(".fw_header"), used))
const struct fw_header g_fw_header = {
    .magic = FW_HEADER_MAGIC,
    .header_version = 1,
    .version = (1u << 16) | (0u << 8) | 0u,
    .image_size = (uint32_t)_image_size,
    .build_id_note = __build_id_note,
};
