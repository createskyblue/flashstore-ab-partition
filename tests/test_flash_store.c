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
    bool fail_erase_a;
    bool fail_erase_b;
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
    memcpy(out, src, len);
    return true;
}

static bool fake_erase(void *ctx, uint32_t addr, size_t len) {
    FakeFlash *f = (FakeFlash *)ctx;
    uint8_t *page = page_range(f, addr, 1);
    if (page == NULL || len != PAGE_SIZE) return false;
    if (addr == PAGE_A_ADDRESS && f->fail_erase_a) return false;
    if (addr == PAGE_B_ADDRESS && f->fail_erase_b) return false;
    memset(page, 0xFF, PAGE_SIZE);
    return true;
}

static bool fake_program(void *ctx, uint32_t addr,
                         const uint8_t *data, size_t len) {
    FakeFlash *f = (FakeFlash *)ctx;
    uint8_t *dst = page_range(f, addr, len);
    if (dst == NULL) return false;
    memcpy(dst, data, len);
    return true;
}

static void make_io(FlashStore_IO *io, FakeFlash *f) {
    io->read    = fake_read;
    io->erase   = fake_erase;
    io->program = fake_program;
    io->context = f;
}

static FlashStore_Config make_config(FakeFlash *f, FlashStore_IO *io) {
    make_io(io, f);

    FlashStore_Config cfg = {
        .io              = io,
        .page_a_address  = PAGE_A_ADDRESS,
        .page_b_address  = PAGE_B_ADDRESS,
        .page_size       = PAGE_SIZE,
    };
    return cfg;
}

static bool contains_bytes(const uint8_t *haystack, size_t haystack_size,
                           const uint8_t *needle, size_t needle_size) {
    if (needle_size > haystack_size) return false;
    for (size_t i = 0; i <= haystack_size - needle_size; ++i)
        if (memcmp(haystack + i, needle, needle_size) == 0) return true;
    return false;
}

/* ======================================================================
 * Tests
 * ====================================================================== */

static void test_round_trip(void) {
    FakeFlash f;
    uint8_t out[7] = {0};
    const uint8_t in[7] = {1, 3, 5, 7, 9, 11, 13};

    memset(&f, 0, sizeof(f));
    FlashStore_IO io;
    FlashStore_Config cfg = make_config(&f, &io);

    assert(FlashStore_Save(&cfg, in, sizeof(in)) == FLASH_STORE_OK);
    assert(memcmp(f.page_a, f.page_b, PAGE_SIZE) == 0);
    assert(contains_bytes(f.page_a, PAGE_SIZE, in, sizeof(in)));
    assert(FlashStore_Load(&cfg, out, sizeof(out)) == FLASH_STORE_OK);
    assert(memcmp(out, in, sizeof(in)) == 0);
}

static void test_corrupt_primary_falls_back_to_secondary(void) {
    FakeFlash f;
    uint8_t out[4] = {0};
    const uint8_t in[4] = {10, 20, 30, 40};

    memset(&f, 0, sizeof(f));
    FlashStore_IO io;
    FlashStore_Config cfg = make_config(&f, &io);

    assert(FlashStore_Save(&cfg, in, sizeof(in)) == FLASH_STORE_OK);
    f.page_a[HEADER_SIZE] ^= 0x80u;   /* corrupt A payload */
    assert(FlashStore_Load(&cfg, out, sizeof(out)) == FLASH_STORE_OK);
    assert(memcmp(out, in, sizeof(in)) == 0);
    assert(memcmp(f.page_a, f.page_b, PAGE_SIZE) == 0);
}

static void test_corrupt_backup_repairs_from_primary(void) {
    FakeFlash f;
    uint8_t out[4] = {0};
    const uint8_t in[4] = {0xAA, 0xBB, 0xCC, 0xDD};

    memset(&f, 0, sizeof(f));
    FlashStore_IO io;
    FlashStore_Config cfg = make_config(&f, &io);

    assert(FlashStore_Save(&cfg, in, sizeof(in)) == FLASH_STORE_OK);
    f.page_b[HEADER_SIZE + 1] ^= 0x40u;  /* corrupt B payload */
    assert(memcmp(f.page_a, f.page_b, PAGE_SIZE) != 0);
    assert(FlashStore_Load(&cfg, out, sizeof(out)) == FLASH_STORE_OK);
    assert(memcmp(out, in, sizeof(in)) == 0);
    assert(memcmp(f.page_a, f.page_b, PAGE_SIZE) == 0);
}

static void test_failed_write_A_preserves_old_B(void) {
    FakeFlash f;
    uint8_t out[4] = {0};
    const uint8_t old[4] = {1, 2, 3, 4};
    const uint8_t newer[4] = {5, 6, 7, 8};

    memset(&f, 0, sizeof(f));
    FlashStore_IO io;
    FlashStore_Config cfg = make_config(&f, &io);

    assert(FlashStore_Save(&cfg, old, sizeof(old)) == FLASH_STORE_OK);
    f.fail_erase_a = true;
    assert(FlashStore_Save(&cfg, newer, sizeof(newer)) == FLASH_STORE_ERROR_WRITE);
    assert(FlashStore_Load(&cfg, out, sizeof(out)) == FLASH_STORE_OK);
    assert(memcmp(out, old, sizeof(old)) == 0);
}

/*
 * A written with v2, B still holds v1 from previous save (B erase failed).
 * Both CRC-valid but different payloads — A is newer because A is always
 * written first.  CRC comparison detects the difference and repairs B.
 */
static void test_A_newer_CRC_diff_repairs_B(void) {
    FakeFlash f;
    uint8_t out[4] = {0};
    const uint8_t v1[4] = {1, 2, 3, 4};
    const uint8_t v2[4] = {0xAA, 0xBB, 0xCC, 0xDD};

    memset(&f, 0, sizeof(f));
    FlashStore_IO io;
    FlashStore_Config cfg = make_config(&f, &io);

    /* first save — A and B both have v1 */
    assert(FlashStore_Save(&cfg, v1, sizeof(v1)) == FLASH_STORE_OK);

    /* second save — write A (v2), then B erase fails → B still v1 */
    f.fail_erase_b = true;
    assert(FlashStore_Save(&cfg, v2, sizeof(v2)) == FLASH_STORE_ERROR_WRITE);
    f.fail_erase_b = false;

    /* A=v2, B=v1 — both CRC-valid, different content */
    assert(FlashStore_Load(&cfg, out, sizeof(out)) == FLASH_STORE_OK);
    assert(memcmp(out, v2, sizeof(v2)) == 0);          /* returned A (newer) */
    assert(memcmp(f.page_a, f.page_b, PAGE_SIZE) == 0); /* B repaired */
}

static void test_both_pages_corrupt_returns_error(void) {
    FakeFlash f;
    uint8_t out[4] = {0};
    const uint8_t in[4] = {7, 7, 7, 7};

    memset(&f, 0, sizeof(f));
    FlashStore_IO io;
    FlashStore_Config cfg = make_config(&f, &io);

    assert(FlashStore_Save(&cfg, in, sizeof(in)) == FLASH_STORE_OK);
    memset(f.page_a, 0, 4);  /* kill magic */
    memset(f.page_b, 0, 4);
    assert(FlashStore_Load(&cfg, out, sizeof(out)) == FLASH_STORE_ERROR_NO_VALID_DATA);
}

static void test_max_data_size(void) {
    FakeFlash f;
    memset(&f, 0, sizeof(f));
    FlashStore_IO io;
    FlashStore_Config cfg = make_config(&f, &io);

    size_t max = FlashStore_MaxDataSize(&cfg);
    assert(max == PAGE_SIZE - HEADER_SIZE);
    assert(max > 0);
    assert(FlashStore_MaxDataSize(NULL) == 0);
}

static void test_save_load_rejects_bad_args(void) {
    FakeFlash f;
    uint8_t buf[4] = {0};
    const uint8_t data[4] = {1, 2, 3, 4};

    memset(&f, 0, sizeof(f));
    FlashStore_IO io;
    FlashStore_Config cfg = make_config(&f, &io);

    assert(FlashStore_Save(NULL, data, sizeof(data)) == FLASH_STORE_ERROR_ARGUMENT);
    assert(FlashStore_Save(&cfg, NULL, sizeof(data)) == FLASH_STORE_ERROR_ARGUMENT);
    assert(FlashStore_Save(&cfg, data, 0) == FLASH_STORE_ERROR_ARGUMENT);
    assert(FlashStore_Load(NULL, buf, sizeof(buf)) == FLASH_STORE_ERROR_ARGUMENT);
    assert(FlashStore_Load(&cfg, NULL, sizeof(buf)) == FLASH_STORE_ERROR_ARGUMENT);
    assert(FlashStore_Load(&cfg, buf, 0) == FLASH_STORE_ERROR_ARGUMENT);

    size_t max = FlashStore_MaxDataSize(&cfg);
    uint8_t big[PAGE_SIZE];
    assert(FlashStore_Save(&cfg, big, max + 1) == FLASH_STORE_ERROR_ARGUMENT);
    assert(FlashStore_Load(&cfg, big, max + 1) == FLASH_STORE_ERROR_ARGUMENT);
}

static void test_config_check_rejects_bad_args(void) {
    FlashStore_IO io;
    FakeFlash f;
    memset(&f, 0, sizeof(f));
    make_io(&io, &f);

    FlashStore_Config cfg = {
        .io = &io,
        .page_a_address = PAGE_A_ADDRESS,
        .page_b_address = PAGE_B_ADDRESS,
        .page_size = PAGE_SIZE,
    };

    assert(FlashStore_ConfigCheck(NULL) == FLASH_STORE_ERROR_ARGUMENT);
    cfg.io = NULL;
    assert(FlashStore_ConfigCheck(&cfg) == FLASH_STORE_ERROR_ARGUMENT);
    cfg.io = &io;
    io.read = NULL;
    assert(FlashStore_ConfigCheck(&cfg) == FLASH_STORE_ERROR_ARGUMENT);
    io.read = fake_read;
    io.erase = NULL;
    assert(FlashStore_ConfigCheck(&cfg) == FLASH_STORE_ERROR_ARGUMENT);
    io.erase = fake_erase;
    cfg.page_a_address = cfg.page_b_address;
    assert(FlashStore_ConfigCheck(&cfg) == FLASH_STORE_ERROR_ARGUMENT);
    cfg.page_a_address = PAGE_A_ADDRESS;
    cfg.page_size = HEADER_SIZE;
    assert(FlashStore_ConfigCheck(&cfg) == FLASH_STORE_ERROR_ARGUMENT);
    cfg.page_size = PAGE_SIZE;
    cfg.page_a_address = PAGE_A_ADDRESS + 1;
    assert(FlashStore_ConfigCheck(&cfg) == FLASH_STORE_ERROR_ARGUMENT);
    cfg.page_a_address = PAGE_A_ADDRESS;
    cfg.page_b_address = PAGE_B_ADDRESS + 3;
    assert(FlashStore_ConfigCheck(&cfg) == FLASH_STORE_ERROR_ARGUMENT);
    cfg.page_b_address = PAGE_B_ADDRESS;
    cfg.page_a_address = 0x1000;
    cfg.page_b_address = 0x1040;
    assert(FlashStore_ConfigCheck(&cfg) == FLASH_STORE_ERROR_ARGUMENT);
}

static void test_warn_repair_failed(void) {
    FakeFlash f;
    uint8_t out[4] = {0};
    const uint8_t in[4] = {0x11, 0x22, 0x33, 0x44};

    memset(&f, 0, sizeof(f));
    FlashStore_IO io;
    FlashStore_Config cfg = make_config(&f, &io);

    assert(FlashStore_Save(&cfg, in, sizeof(in)) == FLASH_STORE_OK);
    f.page_a[HEADER_SIZE] ^= 0xFF;   /* corrupt A payload */
    f.fail_erase_a = true;            /* repair won't be able to erase A */

    FlashStore_Status status = FlashStore_Load(&cfg, out, sizeof(out));
    assert(memcmp(out, in, sizeof(in)) == 0);             /* data correct */
    assert(status == FLASH_STORE_WARN_REPAIR_FAILED);     /* but repair failed */
}

int main(void) {
    test_round_trip();
    test_corrupt_primary_falls_back_to_secondary();
    test_corrupt_backup_repairs_from_primary();
    test_failed_write_A_preserves_old_B();
    test_A_newer_CRC_diff_repairs_B();
    test_both_pages_corrupt_returns_error();
    test_max_data_size();
    test_save_load_rejects_bad_args();
    test_config_check_rejects_bad_args();
    test_warn_repair_failed();
    puts("flash_store tests passed");
    return 0;
}
