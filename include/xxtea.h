#ifndef XXTEA_H
#define XXTEA_H

#include <stddef.h>
#include <stdint.h>

/*
 * XXTEA — Corrected Block TEA
 * David Wheeler & Roger Needham, Cambridge, 1998
 *
 * Preconditions (enforced by assert in debug builds):
 *   - `data` must be 4-byte aligned
 *   - `size` must be a multiple of 4 and >= 8 bytes
 *
 * The caller is responsible for padding the input buffer before
 * encryption and ensuring the buffer is large enough for decryption.
 */
void xxtea_encrypt(uint8_t *data, size_t size, const uint32_t k[4]);
void xxtea_decrypt(uint8_t *data, size_t size, const uint32_t k[4]);

#endif
