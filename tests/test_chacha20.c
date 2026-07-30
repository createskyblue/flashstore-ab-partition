#include "chacha20.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* RFC 8439 §2.4.2 — IETF ChaCha20 encryption test vector.
 * This is the definitive test: key, nonce, counter=1, known plaintext
 * produces known ciphertext.  Covers block function, state setup, and
 * XOR all in one. */
static void test_rfc8439_encryption(void) {
    const uint8_t key[32] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    };
    const uint8_t nonce[12] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4a,
        0x00, 0x00, 0x00, 0x00,
    };
    const uint8_t plaintext[] =
        "Ladies and Gentlemen of the class of '99: "
        "If I could offer you only one tip for the future, "
        "sunscreen would be it.";
    const uint8_t expected[] = {
        0x6e, 0x2e, 0x35, 0x9a, 0x25, 0x68, 0xf9, 0x80,
        0x41, 0xba, 0x07, 0x28, 0xdd, 0x0d, 0x69, 0x81,
        0xe9, 0x7e, 0x7a, 0xec, 0x1d, 0x43, 0x60, 0xc2,
        0x0a, 0x27, 0xaf, 0xcc, 0xfd, 0x9f, 0xae, 0x0b,
        0xf9, 0x1b, 0x65, 0xc5, 0x52, 0x47, 0x33, 0xab,
        0x8f, 0x59, 0x3d, 0xab, 0xcd, 0x62, 0xb3, 0x57,
        0x16, 0x39, 0xd6, 0x24, 0xe6, 0x51, 0x52, 0xab,
        0x8f, 0x53, 0x0c, 0x35, 0x9f, 0x08, 0x61, 0xd8,
        0x07, 0xca, 0x0d, 0xbf, 0x50, 0x0d, 0x6a, 0x61,
        0x56, 0xa3, 0x8e, 0x08, 0x8a, 0x22, 0xb6, 0x5e,
        0x52, 0xbc, 0x51, 0x4d, 0x16, 0xcc, 0xf8, 0x06,
        0x81, 0x8c, 0xe9, 0x1a, 0xb7, 0x79, 0x37, 0x36,
        0x5a, 0xf9, 0x0b, 0xbf, 0x74, 0xa3, 0x5b, 0xe6,
        0xb4, 0x0b, 0x8e, 0xed, 0xf2, 0x78, 0x5e, 0x42,
        0x87, 0x4d,
    };
    size_t len = sizeof(plaintext) - 1;  /* exclude NUL */

    uint8_t buf[128];
    memcpy(buf, plaintext, len);
    chacha20_crypt(buf, len, key, nonce);
    assert(memcmp(buf, expected, len) == 0);

    /* decrypt back to plaintext */
    chacha20_crypt(buf, len, key, nonce);
    assert(memcmp(buf, plaintext, len) == 0);
}

/* Test cross-block encryption: >64 bytes forces counter increment.
 * The RFC 8439 §2.4.2 test (114 bytes) already crosses one block
 * boundary — this tests crossing multiple boundaries. */
static void test_cross_block(void) {
    const uint8_t key[32] = {0x55};
    const uint8_t nonce[12] = {0xaa};
    uint8_t buf[256], orig[256];

    for (size_t i = 0; i < sizeof(buf); i++)
        buf[i] = (uint8_t)(i * 7 + 31);
    memcpy(orig, buf, sizeof(buf));

    chacha20_crypt(buf, sizeof(buf), key, nonce);
    assert(memcmp(buf, orig, sizeof(buf)) != 0);

    chacha20_crypt(buf, sizeof(buf), key, nonce);
    assert(memcmp(buf, orig, sizeof(buf)) == 0);
}

static void test_round_trip(void) {
    const uint8_t key[32] = {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
        0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    };
    const uint8_t nonce[12] = {
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
    };

    /* Stream cipher — any size works, no alignment needed */
    uint8_t sizes[] = {1, 3, 7, 8, 13, 16, 31, 64, 65, 100, 127, 128, 129};
    uint8_t buf[256], orig[256];

    for (int s = 0; s < (int)(sizeof(sizes) / sizeof(sizes[0])); s++) {
        size_t len = sizes[s];
        for (size_t i = 0; i < len; i++)
            buf[i] = (uint8_t)(i * 3 + 17);
        memcpy(orig, buf, len);

        chacha20_crypt(buf, len, key, nonce);
        assert(memcmp(buf, orig, len) != 0);

        chacha20_crypt(buf, len, key, nonce);
        assert(memcmp(buf, orig, len) == 0);
    }
}

static void test_different_nonce_produces_different_output(void) {
    const uint8_t key[32] = {0};
    const uint8_t data[] = "hello world";
    uint8_t buf1[16], buf2[16];
    size_t len = sizeof(data) - 1;

    memcpy(buf1, data, len);
    memcpy(buf2, data, len);

    const uint8_t nonce1[12] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const uint8_t nonce2[12] = {2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    chacha20_crypt(buf1, len, key, nonce1);
    chacha20_crypt(buf2, len, key, nonce2);

    assert(memcmp(buf1, buf2, len) != 0);
}

int main(void) {
    test_rfc8439_encryption();
    test_cross_block();
    test_round_trip();
    test_different_nonce_produces_different_output();
    puts("chacha20 tests passed");
    return 0;
}
