/*
 * Lab 9: the Non-secure Callable API of the Secure world.
 *
 * The Non-secure image links against the import library the secure build
 * produces (lab09_*_nsclib.o), which contains only the addresses of these
 * functions' veneers in the NSC region.
 */
#ifndef SECURE_API_H
#define SECURE_API_H

#include <stdint.h>

#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3)
#define SECURE_ENTRY __attribute__((cmse_nonsecure_entry))
#else
#define SECURE_ENTRY
#endif

#define SECURE_MAC_LEN 32u

/* HMAC-SHA256 of msg[0..len) with the device key, written to mac[0..32).
 * Returns 0, or -1 if a buffer is not entirely Non-secure memory. */
SECURE_ENTRY int32_t secure_sign(const void *msg, uint32_t len, uint8_t *mac);

/* A "debug leftover": tells the caller where the key lives. Real products
 * ship with functions like this more often than you'd think. */
SECURE_ENTRY uint32_t secure_debug_key_address(void);

#endif
