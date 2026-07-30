#ifndef XXTEA_H
#define XXTEA_H

#include <stddef.h>
#include <stdint.h>

/*
 * XXTEA — Corrected Block TEA
 * David Wheeler & Roger Needham, Cambridge, 1998
 * http://www.cix.co.uk/~klockstone/xxtea.pdf
 *
 * `data` must be word-aligned (size is a multiple of 4) and at least
 * 8 bytes.  The caller is responsible for padding.
 */
void xxtea_encrypt(uint8_t *data, size_t size, const uint32_t k[4]);
void xxtea_decrypt(uint8_t *data, size_t size, const uint32_t k[4]);

#endif
