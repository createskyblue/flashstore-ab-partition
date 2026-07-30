#include "flash_store.h"
#include "xxtea.h"

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

static void init_config(FlashStore_Config *config, FakeFlash *flash,
                        uint8_t *workspace,
                        const uint32_t *cipher_key) {
    memset(config, 0, sizeof(*config));
    config->io.read    = fake_read;
    config->io.erase   = fake_erase;
    config->io.program = fake_program;
    config->context         = flash;
    config->page_a_address  = PAGE_A_ADDRESS;
    config->page_b_address  = PAGE_B_ADDRESS;
    config->page_size       = PAGE_SIZE;
    config->workspace       = workspace;
    if (cipher_key != NULL) {
        config->encrypt    = xxtea_encrypt;
        config->decrypt    = xxtea_decrypt;
        config->cipher_key = cipher_key;
    }
}

static FlashStore make_store(FakeFlash *flash, uint8_t *workspace,
                             const uint32_t *cipher_key) {
    FlashStore store;
    FlashStore_Config config;
    init_config(&config, flash, workspace, cipher_key);
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
    const uint32_t key[4] = {0x12345678u, 0x9ABCDEF0u,
                             0x0FEDCBA9u, 0x87654321u};
    memset(&flash, 0xFF, sizeof(flash));
    flash.fail_program_address = 0;
    FlashStore store = make_store(&flash, workspace, key);

    assert(FlashStore_Save(&store, input, sizeof(input)) == FLASH_STORE_OK);
    assert(memcmp(flash.page_a, flash.page_b, PAGE_SIZE) == 0);
    assert(!contains_bytes(flash.page_a, PAGE_SIZE, input, sizeof(input)));
    assert(FlashStore_Load(&store, output, sizeof(output)) == FLASH_STORE_OK);
    assert(memcmp(output, input, sizeof(input)) == 0);
}

static void test_corrupt_primary_falls_back_to_secondary(void) {
    FakeFlash flash;
    uint8_t workspace[PAGE_SIZE];
    uint8_t output[4] = {0};
    const uint8_t input[4] = {10, 20, 30, 40};
    const uint32_t key[4] = {99u, 0u, 0u, 0u};
    memset(&flash, 0xFF, sizeof(flash));
    flash.fail_program_address = 0;
    FlashStore store = make_store(&flash, workspace, key);

    assert(FlashStore_Save(&store, input, sizeof(input)) == FLASH_STORE_OK);
    flash.read_count = 0;
    flash.page_a[12] ^= 0x80u;
    assert(FlashStore_Load(&store, output, sizeof(output)) == FLASH_STORE_OK);
    assert(flash.read_count == 2);
    assert(memcmp(output, input, sizeof(input)) == 0);
}

static void test_failed_primary_update_preserves_old_secondary(void) {
    FakeFlash flash;
    uint8_t workspace[PAGE_SIZE];
    uint8_t output[4] = {0};
    const uint8_t old_value[4] = {1, 2, 3, 4};
    const uint8_t new_value[4] = {5, 6, 7, 8};
    const uint32_t key[4] = {7u, 0u, 0u, 0u};
    memset(&flash, 0xFF, sizeof(flash));
    flash.fail_program_address = 0;
    FlashStore store = make_store(&flash, workspace, key);

    assert(FlashStore_Save(&store, old_value, sizeof(old_value)) == FLASH_STORE_OK);
    flash.fail_program_address = PAGE_A_ADDRESS;
    assert(FlashStore_Save(&store, new_value, sizeof(new_value)) == FLASH_STORE_ERROR_WRITE);
    assert(FlashStore_Load(&store, output, sizeof(output)) == FLASH_STORE_OK);
    assert(memcmp(output, old_value, sizeof(old_value)) == 0);
}

static void test_plaintext_no_cipher(void) {
    FakeFlash flash;
    uint8_t workspace[PAGE_SIZE];
    uint8_t output[4] = {0};
    const uint8_t input[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    memset(&flash, 0xFF, sizeof(flash));
    flash.fail_program_address = 0;
    FlashStore store = make_store(&flash, workspace, NULL);  /* no cipher */

    assert(FlashStore_Save(&store, input, sizeof(input)) == FLASH_STORE_OK);
    /* plaintext must be visible in flash when no cipher is configured */
    assert(contains_bytes(flash.page_a, PAGE_SIZE, input, sizeof(input)));
    assert(FlashStore_Load(&store, output, sizeof(output)) == FLASH_STORE_OK);
    assert(memcmp(output, input, sizeof(input)) == 0);
}

int main(void) {
    test_round_trip_uses_both_pages();
    test_corrupt_primary_falls_back_to_secondary();
    test_failed_primary_update_preserves_old_secondary();
    test_plaintext_no_cipher();
    puts("flash_store tests passed");
    return 0;
}
