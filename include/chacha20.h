#ifndef CHACHA20_H
#define CHACHA20_H

#include <stddef.h>
#include <stdint.h>

/*
 * ChaCha20 stream cipher (RFC 8439).
 *
 * Encrypts or decrypts data in-place.  Same operation both ways.
 *
 *   key   — 32 bytes (256 bits)
 *   nonce — 12 bytes (96 bits), must be unique per message under the same key
 *
 * No alignment requirements on data.
 *
 * ~2 KB Flash, ~1 KB RAM on a typical Cortex-M MCU.
 *
 * For resource-constrained devices that only need obfuscation,
 * see xxtea.h — smaller but weaker.
 */
void chacha20_crypt(uint8_t *data, size_t size,
                    const uint8_t key[32], const uint8_t nonce[12]);

#endif
