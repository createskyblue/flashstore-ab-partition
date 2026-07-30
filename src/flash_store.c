#include "flash_store.h"

#include <string.h>

enum {
    FLASH_STORE_HEADER_SIZE = 12,
};

static const uint32_t FLASH_STORE_MAGIC = 0x46534142u;

static void write_u32(uint8_t *dst, uint32_t value) {
    memcpy(dst, &value, sizeof(value));
}

static uint32_t read_u32(const uint8_t *src) {
    uint32_t value;
    memcpy(&value, src, sizeof(value));
    return value;
}

static uint32_t crc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

/* ---- per-page save / load -------------------------------------------- */

static FlashStore_Status save_page(FlashStore *store, uint32_t address,
                                   const uint8_t *data, size_t size) {
    /* build header on stack — no workspace needed */
    uint8_t header[FLASH_STORE_HEADER_SIZE];
    write_u32(header, FLASH_STORE_MAGIC);
    write_u32(header + 4, (uint32_t)size);
    write_u32(header + 8, crc32(data, size));

    if (!store->config.io.erase(store->config.context,
                                address, store->config.page_size))
        return FLASH_STORE_ERROR_WRITE;
    if (!store->config.io.program(store->config.context,
                                  address, header, sizeof(header)))
        return FLASH_STORE_ERROR_WRITE;
    if (!store->config.io.program(store->config.context,
                                  address + FLASH_STORE_HEADER_SIZE,
                                  data, size))
        return FLASH_STORE_ERROR_WRITE;
    return FLASH_STORE_OK;
}

static bool load_page(FlashStore *store, uint32_t address,
                      uint8_t *data, size_t size) {
    uint8_t header[FLASH_STORE_HEADER_SIZE];

    if (!store->config.io.read(store->config.context,
                               address, header, sizeof(header)))
        return false;
    if (read_u32(header) != FLASH_STORE_MAGIC)
        return false;
    if (read_u32(header + 4) != size)
        return false;
    if (!store->config.io.read(store->config.context,
                               address + FLASH_STORE_HEADER_SIZE,
                               data, size))
        return false;
    return read_u32(header + 8) == crc32(data, size);
}

/* ---- public API ------------------------------------------------------ */

FlashStore_Status FlashStore_Init(FlashStore *store,
                                  const FlashStore_Config *config) {
    if (store == NULL || config == NULL || config->io.read == NULL ||
        config->io.erase == NULL || config->io.program == NULL ||
        config->page_a_address == config->page_b_address ||
        config->page_size <= FLASH_STORE_HEADER_SIZE) {
        return FLASH_STORE_ERROR_ARGUMENT;
    }

    store->config = *config;
    return FLASH_STORE_OK;
}

size_t FlashStore_MaxDataSize(const FlashStore *store) {
    if (store == NULL || store->config.page_size <= FLASH_STORE_HEADER_SIZE)
        return 0;
    return store->config.page_size - FLASH_STORE_HEADER_SIZE;
}

FlashStore_Status FlashStore_Save(FlashStore *store,
                                  const uint8_t *data, size_t size) {
    if (store == NULL || data == NULL ||
        size == 0 || size > FlashStore_MaxDataSize(store))
        return FLASH_STORE_ERROR_ARGUMENT;

    FlashStore_Status status = save_page(store, store->config.page_a_address,
                                         data, size);
    if (status != FLASH_STORE_OK) return status;

    return save_page(store, store->config.page_b_address, data, size);
}

FlashStore_Status FlashStore_Load(FlashStore *store,
                                  uint8_t *data, size_t size) {
    if (store == NULL || data == NULL ||
        size == 0 || size > FlashStore_MaxDataSize(store))
        return FLASH_STORE_ERROR_ARGUMENT;

    const uint32_t addresses[] = {
        store->config.page_a_address,
        store->config.page_b_address,
    };
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        if (load_page(store, addresses[i], data, size))
            return FLASH_STORE_OK;
    }
    return FLASH_STORE_ERROR_NO_VALID_DATA;
}
