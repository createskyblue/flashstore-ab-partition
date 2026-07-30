#include "xxtea.h"

#include <assert.h>
#include <string.h>

#define XXTEA_DELTA 0x9E3779B9u

#define XXTEA_MX(z, y, p, sum, e, k)                                 \
    ((((z) >> 5 ^ (y) << 2) + ((y) >> 3 ^ (z) << 4))                 \
     ^ (((sum) ^ (y)) + ((k)[((p) & 3) ^ (e)] ^ (z))))

/* memcpy-based word access — safe on architectures that don't support
 * unaligned loads (Cortex-M0, etc.). */
static uint32_t ld32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static void st32(uint8_t *p, uint32_t v) {
    memcpy(p, &v, sizeof(v));
}

void xxtea_encrypt(uint8_t *data, size_t size, const uint32_t k[4]) {
    assert(data != NULL);
    assert((uintptr_t)data % 4 == 0);
    assert(size >= 8 && size % 4 == 0);

    int n = (int)(size / 4);
    unsigned rounds = 6 + 52 / n;
    uint32_t y, z, sum = 0;

    z = ld32(data + (n - 1) * 4);
    do {
        sum += XXTEA_DELTA;
        unsigned e = (sum >> 2) & 3;
        int p;
        for (p = 0; p < n - 1; p++) {
            y = ld32(data + (p + 1) * 4);
            z = ld32(data + p * 4) + XXTEA_MX(z, y, p, sum, e, k);
            st32(data + p * 4, z);
        }
        y = ld32(data);
        z = ld32(data + (n - 1) * 4) + XXTEA_MX(z, y, n - 1, sum, e, k);
        st32(data + (n - 1) * 4, z);
    } while (--rounds);
}

void xxtea_decrypt(uint8_t *data, size_t size, const uint32_t k[4]) {
    assert(data != NULL);
    assert((uintptr_t)data % 4 == 0);
    assert(size >= 8 && size % 4 == 0);

    int n = (int)(size / 4);
    unsigned rounds = 6 + 52 / n;
    uint32_t y, z, sum = rounds * XXTEA_DELTA;

    y = ld32(data);
    do {
        unsigned e = (sum >> 2) & 3;
        int p;
        for (p = n - 1; p > 0; p--) {
            z = ld32(data + (p - 1) * 4);
            y = ld32(data + p * 4) - XXTEA_MX(z, y, p, sum, e, k);
            st32(data + p * 4, y);
        }
        z = ld32(data + (n - 1) * 4);
        y = ld32(data) - XXTEA_MX(z, y, 0, sum, e, k);
        st32(data, y);
        sum -= XXTEA_DELTA;
    } while (--rounds);
}
