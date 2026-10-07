/*
 * Lab 10: the bootloader. Complete TODO 1-4.
 *
 * Runs from sector 0 on the 64 MHz HSI reset clock, so the application
 * finds the clock tree exactly as after reset.
 *
 *   1. Update requested (magic in SRAM4 from the app, or B1 held at reset)?
 *      -> receive a signed image over USART3 into the inactive slot.
 *   2. Read the boot state. None: pick the best valid slot.
 *   3. PENDING (a new image on trial): count the attempt; after
 *      MAX_ATTEMPTS without a confirm, revert to the other slot.
 *   4. Verify the active slot's signature and anti-rollback counter.
 *   5. Start the independent watchdog, jump.
 */
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "boot_state.h"
#include "flash_h7.h"
#include "flash_layout.h"
#include "image.h"
#include "monocypher-ed25519.h"
#include "pubkey.h"

#define MAX_ATTEMPTS 3u
#define CHUNK 256u
#define RX_TIMEOUT_MS 10000u

SRAM4_NOINIT static volatile uint32_t g_update_request;

/* ------------------------------------------------------------------ */
/* Image verification                                                  */
/* ------------------------------------------------------------------ */

static const struct image_header *slot_header(unsigned slot)
{
    return (const struct image_header *)SLOT_BASE(slot);
}

/* Structure checks, the anti-rollback counter, then the signature over
 * SHA-512(header without signature || payload). */
static bool image_valid(unsigned slot, uint32_t min_counter)
{
    /* TODO 1: return true only if the slot holds a genuine image:
     *   - header fields sane: magic, header_size, slot == this slot,
     *     0 < payload_size <= SLOT_SIZE - IMAGE_HEADER_SIZE
     *   - security_counter >= min_counter (anti-rollback)
     *   - signature: SHA-512 over the header up to the signature field, then
     *     the payload (crypto_sha512_init/update/final), then
     *     crypto_ed25519_check(h->signature, k_signer_pubkey, digest, 64) == 0
     * Until then every image is "valid": try flashing a corrupted one. */
    const struct image_header *h = slot_header(slot);
    (void)min_counter;
    return h->magic == IMAGE_MAGIC;
}

/* The valid slot with the highest version, or -1. */
static int best_slot(uint32_t min_counter)
{
    int best = -1;
    for (unsigned s = SLOT_A; s <= SLOT_B; s++) {
        if (image_valid(s, min_counter) &&
            (best < 0 || slot_header(s)->version > slot_header((unsigned)best)->version)) {
            best = (int)s;
        }
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* Jump                                                                */
/* ------------------------------------------------------------------ */

static void iwdg_start(void)
{
    /* TODO 2: start IWDG1 with a ~4 s timeout (LSI 32 kHz): KR = 0xCCCC to
     * start, 0x5555 to unlock PR/RLR, set the prescaler and reload, wait for
     * SR == 0, refresh with 0xAAAA. Why must the bootloader, not the app,
     * start it? */
}

__attribute__((noreturn)) static void jump_to_slot(unsigned slot)
{
    const uint32_t *vectors = (const uint32_t *)(SLOT_BASE(slot) + IMAGE_HEADER_SIZE);
    printf("BOOT: jumping to slot %c\n", (int)('A' + slot));
    while (!(USART3->ISR & USART_ISR_TC)) {
    }
    /* TODO 3: hand over a clean core to the application:
     *   - interrupts off, SysTick off, every NVIC enable and pending bit cleared
     *   - USART3 disabled and reset (RCC->APB1LRSTR), as after reset
     *   - VTOR = vectors, DSB, ISB
     *   - MSP = vectors[0], interrupts back on, branch to vectors[1] */
    (void)vectors;
    for (;;) {
    }
}

/* ------------------------------------------------------------------ */
/* Update mode                                                         */
/* ------------------------------------------------------------------ */

static int getc_timeout(uint32_t ms)
{
    uint32_t start = dwt_cycles();
    uint32_t limit = ms * (SystemCoreClock / 1000u);
    for (;;) {
        int c = board_uart_getc();
        if (c >= 0) {
            return c;
        }
        if (dwt_cycles() - start > limit) {
            return -1;
        }
    }
}

static bool read_exact(uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int c = getc_timeout(RX_TIMEOUT_MS);
        if (c < 0) {
            return false;
        }
        buf[i] = (uint8_t)c;
    }
    return true;
}

static uint16_t crc16(const uint8_t *d, size_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= (uint16_t)(*d++ << 8);
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/*
 * Protocol (tools/fw_update.py):
 *   host: "UPD" + u32 size       boot: erases the target slot, then 'R'
 *   host: 0x55 seq:u16 len:u16 data[len] crc16:u16   boot: 'A' (or 'N': resend)
 *   host: the same frame with len = 0                 boot: 'V' valid / 'X' invalid
 * A power cut at any point leaves the old image untouched and active: the
 * new one only becomes PENDING after its signature has been checked.
 */
__attribute__((noreturn)) static void update_mode(unsigned target, uint32_t min_counter)
{
    printf("BOOT: update mode, target slot %c. Waiting for image...\n", (int)('A' + target));

    /* Wait for "UPD" and the size. */
    uint8_t hdr[7];
    for (;;) {
        if (!read_exact(hdr, 1) || hdr[0] != 'U') {
            continue;
        }
        if (read_exact(hdr + 1, 6) && hdr[1] == 'P' && hdr[2] == 'D') {
            break;
        }
    }
    uint32_t size = (uint32_t)hdr[3] | ((uint32_t)hdr[4] << 8) | ((uint32_t)hdr[5] << 16) |
                    ((uint32_t)hdr[6] << 24);
    if (size == 0 || size > SLOT_SIZE) {
        board_uart_putc('X');
        NVIC_SystemReset();
    }

    for (unsigned i = 0; i < SLOT_SECTORS; i++) {
        flash_erase_sector(SLOT_FIRST_SECTOR(target) + i);
    }
    board_uart_putc('R');

    static uint8_t frame[5 + CHUNK + 2];
    static uint32_t words[CHUNK / 4];
    uint32_t expected_seq = 0;
    for (;;) {
        do {
            if (!read_exact(frame, 1)) {
                NVIC_SystemReset(); /* host gone: boot the old image */
            }
        } while (frame[0] != 0x55);
        if (!read_exact(frame + 1, 4)) {
            NVIC_SystemReset();
        }
        uint16_t seq = (uint16_t)(frame[1] | (frame[2] << 8));
        uint16_t len = (uint16_t)(frame[3] | (frame[4] << 8));
        if (len > CHUNK || !read_exact(frame + 5, len + 2u)) {
            board_uart_putc('N');
            continue;
        }
        uint16_t crc = (uint16_t)(frame[5 + len] | (frame[6 + len] << 8));
        if (crc16(frame + 1, 4u + len) != crc) {
            board_uart_putc('N');
            continue;
        }
        if (len == 0) {
            break;
        }
        if (seq == (uint16_t)(expected_seq - 1u)) {
            board_uart_putc('A'); /* a resend after a lost ACK: already written */
            continue;
        }
        if (seq != (uint16_t)expected_seq) {
            board_uart_putc('N');
            continue;
        }
        memset(words, 0xFF, sizeof words);
        memcpy(words, frame + 5, len);
        uint32_t addr = SLOT_BASE(target) + expected_seq * CHUNK;
        bool ok = true;
        for (uint32_t off = 0; off < (len + 31u) / 32u * 32u && ok; off += 32u) {
            ok = flash_program_word(addr + off, &words[off / 4u]);
        }
        if (!ok) {
            board_uart_putc('X');
            NVIC_SystemReset();
        }
        expected_seq++;
        board_uart_putc('A');
    }

    if (!image_valid(target, min_counter)) {
        board_uart_putc('X');
        printf("\nBOOT: new image rejected (signature, slot or counter)\n");
        board_delay_ms(20);
        NVIC_SystemReset();
    }
    struct boot_record rec = {
        .slot = target, .state = SLOT_PENDING, .attempts = 0, .min_counter = min_counter};
    boot_state_append(&rec);
    board_uart_putc('V');
    printf("\nBOOT: image accepted, slot %c now PENDING. Rebooting.\n", (int)('A' + target));
    board_delay_ms(20);
    NVIC_SystemReset();
}

/* ------------------------------------------------------------------ */

int main(void)
{
    /* Stay on the 64 MHz HSI reset clock: no board_clock_init(). */
    dwt_init();
    board_uart_init(115200);
    setvbuf(stdout, NULL, _IONBF, 0);
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOCEN;
    (void)RCC->AHB4ENR;

    printf("\nBOOT: Lab 10 bootloader (reset flags 0x%08lx)\n", (unsigned long)RCC->RSR);
    if (RCC->RSR & RCC_RSR_IWDG1RSTF) {
        printf("BOOT: last reset was the watchdog\n");
    }
    RCC->RSR |= RCC_RSR_RMVF;

    struct boot_record st;
    bool have_state = boot_state_read(&st);
    uint32_t min_counter = have_state ? st.min_counter : 0u;
    /* Never overwrite the last confirmed image: update into the other slot,
     * or into the slot of an image that is still on trial. */
    unsigned target = SLOT_A;
    if (have_state) {
        target = st.state == SLOT_PENDING ? st.slot : (st.slot == SLOT_A ? SLOT_B : SLOT_A);
    }

    bool requested = g_update_request == UPDATE_REQUEST_MAGIC || gpio_read(BUTTON_PORT, BUTTON_PIN);
    g_update_request = 0;
    if (requested) {
        update_mode(target, min_counter);
    }

    if (!have_state) {
        int s = best_slot(0);
        if (s < 0) {
            printf("BOOT: no valid image\n");
            update_mode(SLOT_A, 0);
        }
        st = (struct boot_record){.slot = (uint32_t)s, .state = SLOT_CONFIRMED,
                                  .min_counter = slot_header((unsigned)s)->security_counter};
        boot_state_append(&st);
    }

    /* TODO 4: trial boot. If st.state is SLOT_PENDING:
     *   - attempts already >= MAX_ATTEMPTS: the new image never confirmed
     *     itself. Revert: active slot = the other one, CONFIRMED, attempts 0,
     *     append the record.
     *   - otherwise: attempts + 1, append the record, and boot it.
     * Append BEFORE jumping: why does the order matter if the power fails? */

    if (!image_valid(st.slot, st.min_counter)) {
        unsigned other = st.slot == SLOT_A ? SLOT_B : SLOT_A;
        printf("BOOT: slot %c invalid\n", (int)('A' + st.slot));
        if (!image_valid(other, st.min_counter)) {
            update_mode(st.slot, st.min_counter);
        }
        st.slot = other;
        st.state = SLOT_CONFIRMED;
        st.attempts = 0;
        boot_state_append(&st);
    }

    iwdg_start();
    jump_to_slot(st.slot);
}
