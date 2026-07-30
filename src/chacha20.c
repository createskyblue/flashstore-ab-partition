#include "chacha20.h"

#include <string.h>

/*
 * ChaCha20 quarter round — applied to four 32-bit words.
 * The rotations are constant; a decent compiler will inline this.
 */
#define QR(a, b, c, d) do {          \
    (a) += (b); (d) ^= (a);           \
    (d) = ((d) << 16) | ((d) >> 16);  \
    (c) += (d); (b) ^= (c);           \
    (b) = ((b) << 12) | ((b) >> 20);  \
    (a) += (b); (d) ^= (a);           \
    (d) = ((d) << 8)  | ((d) >> 24);  \
    (c) += (d); (b) ^= (c);           \
    (b) = ((b) << 7)  | ((b) >> 25);  \
} while (0)

/* memcpy-based word access — safe on alignment-restricted MCUs. */
static uint32_t ld32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static void st32(uint8_t *p, uint32_t v) {
    memcpy(p, &v, sizeof(v));
}

/*
 * Compute one ChaCha20 block (64 bytes of keystream) into `out`.
 * The caller provides the 16-word state buffer (modified in place
 * during computation, then restored via addition to produce output).
 */
static void chacha20_block(uint8_t out[64], const uint32_t init[16]) {
    uint32_t x[16];
    int i;

    memcpy(x, init, sizeof(x));

    /* 20 rounds = 10 double-rounds */
    for (i = 0; i < 10; i++) {
        QR(x[0], x[4], x[ 8], x[12]);  /* column 0 */
        QR(x[1], x[5], x[ 9], x[13]);  /* column 1 */
        QR(x[2], x[6], x[10], x[14]);  /* column 2 */
        QR(x[3], x[7], x[11], x[15]);  /* column 3 */
        QR(x[0], x[5], x[10], x[15]);  /* diagonal 0 */
        QR(x[1], x[6], x[11], x[12]);  /* diagonal 1 */
        QR(x[2], x[7], x[ 8], x[13]);  /* diagonal 2 */
        QR(x[3], x[4], x[ 9], x[14]);  /* diagonal 3 */
    }

    /* final addition: x[i] + init[i], serialised as little-endian */
    for (i = 0; i < 16; i++) {
        st32(out + i * 4, x[i] + init[i]);
    }
}

void chacha20_crypt(uint8_t *data, size_t size,
                    const uint8_t key[32], const uint8_t nonce[12]) {
    if (size == 0) return;

    /*
     * Initial state (RFC 8439):
     *   0..3   constant "expand 32-byte k"
     *   4..11  key (256 bits)
     *  12      block counter (32 bits, starts at 0)
     *  13..15  nonce (96 bits)
     */
    uint32_t state[16];
    state[0] = 0x61707865u;
    state[1] = 0x3320646eu;
    state[2] = 0x79622d32u;
    state[3] = 0x6b206574u;

    for (int i = 0; i < 8; i++)
        state[4 + i] = ld32(key + i * 4);

    state[12] = 1;  /* block counter (RFC 8439 starts at 1) */

    for (int i = 0; i < 3; i++)
        state[13 + i] = ld32(nonce + i * 4);

    uint8_t keystream[64];
    size_t offset = 0;

    while (offset < size) {
        chacha20_block(keystream, state);

        size_t chunk = size - offset;
        if (chunk > 64) chunk = 64;

        for (size_t i = 0; i < chunk; i++)
            data[offset + i] ^= keystream[i];

        offset += chunk;
        state[12]++;  /* increment block counter */
    }
}
