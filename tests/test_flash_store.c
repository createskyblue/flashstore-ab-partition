#include "flash_store.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    PAGE_SIZE = 128,
    PAGE_A_ADDRESS = 0x1000,
    PAGE_B_ADDRESS = 0x1080,
    HEADER_SIZE = 12,
};

typedef struct {
    uint8_t page_a[PAGE_SIZE];
    uint8_t page_b[PAGE_SIZE];
    bool fail_erase_a;         /* make erase fail on page A */
    bool fail_program_a;       /* make first program call on A fail */
    unsigned read_count;
} FakeFlash;

static uint8_t *page_range(FakeFlash *f, uint32_t addr, size_t len) {
    if (addr >= PAGE_A_ADDRESS &&
        addr + len <= PAGE_A_ADDRESS + PAGE_SIZE)
        return f->page_a + (addr - PAGE_A_ADDRESS);
    if (addr >= PAGE_B_ADDRESS &&
        addr + len <= PAGE_B_ADDRESS + PAGE_SIZE)
        return f->page_b + (addr - PAGE_B_ADDRESS);
    return NULL;
}

static bool fake_read(void *ctx, uint32_t addr,
                      uint8_t *out, size_t len) {
    FakeFlash *f = (FakeFlash *)ctx;
    uint8_t *src = page_range(f, addr, len);
    if (src == NULL) return false;
    f->read_count++;
    memcpy(out, src, len);
    return true;
}

static bool fake_erase(void *ctx, uint32_t addr, size_t len) {
    FakeFlash *f = (FakeFlash *)ctx;
    uint8_t *page = page_range(f, addr, 1);  /* just check addr in range */
    if (page == NULL || len != PAGE_SIZE) return false;
    if (addr == PAGE_A_ADDRESS && f->fail_erase_a) return false;
    memset(page, 0xFF, PAGE_SIZE);
    return true;
}

static bool fake_program(void *ctx, uint32_t addr,
                         const uint8_t *data, size_t len) {
    FakeFlash *f = (FakeFlash *)ctx;
    uint8_t *dst = page_range(f, addr, len);
    if (dst == NULL) return false;
    if (addr == PAGE_A_ADDRESS && f->fail_program_a) return false;
    memcpy(dst, data, len);
    return true;
}

static FlashStore make_store(FakeFlash *f) {
    FlashStore store;
    FlashStore_Config config = {
        .io = {
            .read   = fake_read,
            .erase  = fake_erase,
            .program = fake_program,
        },
        .context         = f,
        .page_a_address  = PAGE_A_ADDRESS,
        .page_b_address  = PAGE_B_ADDRESS,
        .page_size       = PAGE_SIZE,
    };
    assert(FlashStore_Init(&store, &config) == FLASH_STORE_OK);
    return store;
}

static bool contains_bytes(const uint8_t *haystack, size_t haystack_size,
                           const uint8_t *needle, size_t needle_size) {
    if (needle_size > haystack_size) return false;
    for (size_t i = 0; i <= haystack_size - needle_size; ++i)
        if (memcmp(haystack + i, needle, needle_size) == 0) return true;
    return false;
}

static void test_round_trip(void) {
    FakeFlash f;
    uint8_t out[7] = {0};
    const uint8_t in[7] = {1, 3, 5, 7, 9, 11, 13};

    memset(&f, 0, sizeof(f));
    FlashStore s = make_store(&f);

    assert(FlashStore_Save(&s, in, sizeof(in)) == FLASH_STORE_OK);
    /* both pages identical */
    assert(memcmp(f.page_a, f.page_b, PAGE_SIZE) == 0);
    /* plaintext visible in flash */
    assert(contains_bytes(f.page_a, PAGE_SIZE, in, sizeof(in)));
    /* round-trip */
    assert(FlashStore_Load(&s, out, sizeof(out)) == FLASH_STORE_OK);
    assert(memcmp(out, in, sizeof(in)) == 0);
}

static void test_corrupt_primary_falls_back_to_secondary(void) {
    FakeFlash f;
    uint8_t out[4] = {0};
    const uint8_t in[4] = {10, 20, 30, 40};

    memset(&f, 0, sizeof(f));
    FlashStore s = make_store(&f);

    assert(FlashStore_Save(&s, in, sizeof(in)) == FLASH_STORE_OK);
    f.read_count = 0;
    f.page_a[HEADER_SIZE] ^= 0x80u;   /* corrupt a payload byte on A */
    assert(FlashStore_Load(&s, out, sizeof(out)) == FLASH_STORE_OK);
    assert(f.read_count == 4);         /* header A + data A + header B + data B */
    assert(memcmp(out, in, sizeof(in)) == 0);
    /* primary must be repaired from backup */
    assert(memcmp(f.page_a, f.page_b, PAGE_SIZE) == 0);
}

static void test_failed_write_A_preserves_old_B(void) {
    FakeFlash f;
    uint8_t out[4] = {0};
    const uint8_t old[4] = {1, 2, 3, 4};
    const uint8_t new[4] = {5, 6, 7, 8};

    memset(&f, 0, sizeof(f));
    FlashStore s = make_store(&f);

    /* first save — both pages OK */
    assert(FlashStore_Save(&s, old, sizeof(old)) == FLASH_STORE_OK);

    /* second save — A erase fails, B untouched */
    f.fail_erase_a = true;
    assert(FlashStore_Save(&s, new, sizeof(new)) == FLASH_STORE_ERROR_WRITE);

    /* B still has old data */
    assert(FlashStore_Load(&s, out, sizeof(out)) == FLASH_STORE_OK);
    assert(memcmp(out, old, sizeof(old)) == 0);
}

int main(void) {
    test_round_trip();
    test_corrupt_primary_falls_back_to_secondary();
    test_failed_write_A_preserves_old_B();
    puts("flash_store tests passed");
    return 0;
}
