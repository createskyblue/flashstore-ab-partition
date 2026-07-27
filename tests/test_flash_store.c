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
};

typedef struct {
    uint8_t page_a[PAGE_SIZE];
    uint8_t page_b[PAGE_SIZE];
    uint32_t fail_program_address;
    unsigned read_count;
} FakeFlash;

static uint8_t *page_at(FakeFlash *flash, uint32_t address) {
    if (address == PAGE_A_ADDRESS) {
        return flash->page_a;
    }
    if (address == PAGE_B_ADDRESS) {
        return flash->page_b;
    }
    return NULL;
}

static bool fake_read(void *context, uint32_t address, uint8_t *output, size_t length) {
    FakeFlash *flash = (FakeFlash *)context;
    uint8_t *page = page_at(flash, address);
    if (page == NULL || length != PAGE_SIZE) {
        return false;
    }
    flash->read_count++;
    memcpy(output, page, length);
    return true;
}

static bool fake_erase(void *context, uint32_t address, size_t length) {
    uint8_t *page = page_at((FakeFlash *)context, address);
    if (page == NULL || length != PAGE_SIZE) {
        return false;
    }
    memset(page, 0xFF, length);
    return true;
}

static bool fake_program(void *context, uint32_t address, const uint8_t *data, size_t length) {
    FakeFlash *flash = (FakeFlash *)context;
    uint8_t *page = page_at(flash, address);
    if (page == NULL || length != PAGE_SIZE || address == flash->fail_program_address) {
        return false;
    }
    memcpy(page, data, length);
    return true;
}

static FlashStore make_store(FakeFlash *flash, uint8_t *workspace) {
    FlashStore store;
    FlashStore_Config config = {
        .io = {
            .read = fake_read,
            .erase = fake_erase,
            .program = fake_program,
        },
        .context = flash,
        .page_a_address = PAGE_A_ADDRESS,
        .page_b_address = PAGE_B_ADDRESS,
        .page_size = PAGE_SIZE,
        .workspace = workspace,
        .workspace_size = PAGE_SIZE,
    };
    assert(FlashStore_Init(&store, &config) == FLASH_STORE_OK);
    return store;
}

static bool contains_bytes(
    const uint8_t *haystack,
    size_t haystack_size,
    const uint8_t *needle,
    size_t needle_size
) {
    if (needle_size > haystack_size) {
        return false;
    }
    for (size_t i = 0; i <= haystack_size - needle_size; ++i) {
        if (memcmp(haystack + i, needle, needle_size) == 0) {
            return true;
        }
    }
    return false;
}

static void test_round_trip_uses_both_pages(void) {
    FakeFlash flash;
    uint8_t workspace[PAGE_SIZE];
    uint8_t output[7] = {0};
    const uint8_t input[7] = {1, 3, 5, 7, 9, 11, 13};
    memset(&flash, 0xFF, sizeof(flash));
    flash.fail_program_address = 0;
    FlashStore store = make_store(&flash, workspace);

    assert(FlashStore_Save(&store, 0x12345678u, input, sizeof(input)) == FLASH_STORE_OK);
    assert(memcmp(flash.page_a, flash.page_b, PAGE_SIZE) == 0);
    assert(!contains_bytes(flash.page_a, PAGE_SIZE, input, sizeof(input)));
    assert(FlashStore_Load(&store, 0x12345678u, output, sizeof(output)) == FLASH_STORE_OK);
    assert(memcmp(output, input, sizeof(input)) == 0);
}

static void test_corrupt_primary_falls_back_to_secondary(void) {
    FakeFlash flash;
    uint8_t workspace[PAGE_SIZE];
    uint8_t output[4] = {0};
    const uint8_t input[4] = {10, 20, 30, 40};
    memset(&flash, 0xFF, sizeof(flash));
    flash.fail_program_address = 0;
    FlashStore store = make_store(&flash, workspace);

    assert(FlashStore_Save(&store, 99u, input, sizeof(input)) == FLASH_STORE_OK);
    flash.read_count = 0;
    flash.page_a[12] ^= 0x80u;
    assert(FlashStore_Load(&store, 99u, output, sizeof(output)) == FLASH_STORE_OK);
    assert(flash.read_count == 2);
    assert(memcmp(output, input, sizeof(input)) == 0);
}

static void test_failed_primary_update_preserves_old_secondary(void) {
    FakeFlash flash;
    uint8_t workspace[PAGE_SIZE];
    uint8_t output[4] = {0};
    const uint8_t old_value[4] = {1, 2, 3, 4};
    const uint8_t new_value[4] = {5, 6, 7, 8};
    memset(&flash, 0xFF, sizeof(flash));
    flash.fail_program_address = 0;
    FlashStore store = make_store(&flash, workspace);

    assert(FlashStore_Save(&store, 7u, old_value, sizeof(old_value)) == FLASH_STORE_OK);
    flash.fail_program_address = PAGE_A_ADDRESS;
    assert(FlashStore_Save(&store, 7u, new_value, sizeof(new_value)) == FLASH_STORE_ERROR_WRITE);
    assert(FlashStore_Load(&store, 7u, output, sizeof(output)) == FLASH_STORE_OK);
    assert(memcmp(output, old_value, sizeof(old_value)) == 0);
}

int main(void) {
    test_round_trip_uses_both_pages();
    test_corrupt_primary_falls_back_to_secondary();
    test_failed_primary_update_preserves_old_secondary();
    puts("flash_store tests passed");
    return 0;
}
