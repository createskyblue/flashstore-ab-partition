#include "xxtea.h"

#include <string.h>

#define XXTEA_DELTA 0x9E3779B9u

#define XXTEA_MX(z, y, p, sum, e, k)                                 \
    ((((z) >> 5 ^ (y) << 2) + ((y) >> 3 ^ (z) << 4))                 \
     ^ (((sum) ^ (y)) + ((k)[((p) & 3) ^ (e)] ^ (z))))

/*
 * XXTEA requires at least 2 words (8 bytes).  When the caller passes
 * fewer bytes we transparently pad to 8 bytes.  This is safe because
 * flash_store always operates inside a page-sized workspace — the
 * extra bytes live in the same page and survive a full-page read/write
 * round-trip.
 */
static int xxtea_prepare(uint8_t *data, size_t *size, int encrypt) {
    if (*size >= 8) return (int)(*size / 4);

    if (encrypt) {
        memset(data + *size, 0, 8 - *size);
    }
    *size = 8;
    return 2;
}

void xxtea_encrypt(uint8_t *data, size_t size, const uint32_t k[4]) {
    uint32_t *v = (uint32_t *)data;
    int n = xxtea_prepare(data, &size, 1);
    uint32_t y, z, sum;
    unsigned rounds, e;
    int p;

    rounds = 6 + 52 / n;
    sum = 0;
    z = v[n - 1];
    do {
        sum += XXTEA_DELTA;
        e = (sum >> 2) & 3;
        for (p = 0; p < n - 1; p++) {
            y = v[p + 1];
            z = v[p] += XXTEA_MX(z, y, p, sum, e, k);
        }
        y = v[0];
        z = v[n - 1] += XXTEA_MX(z, y, n - 1, sum, e, k);
    } while (--rounds);
}

void xxtea_decrypt(uint8_t *data, size_t size, const uint32_t k[4]) {
    uint32_t *v = (uint32_t *)data;
    int n = xxtea_prepare(data, &size, 0);
    uint32_t y, z, sum;
    unsigned rounds, e;
    int p;

    rounds = 6 + 52 / n;
    sum = rounds * XXTEA_DELTA;
    y = v[0];
    do {
        e = (sum >> 2) & 3;
        for (p = n - 1; p > 0; p--) {
            z = v[p - 1];
            y = v[p] -= XXTEA_MX(z, y, p, sum, e, k);
        }
        z = v[n - 1];
        y = v[0] -= XXTEA_MX(z, y, 0, sum, e, k);
        sum -= XXTEA_DELTA;
    } while (--rounds);
}
