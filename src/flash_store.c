#include "flash_store.h"

#include <string.h>

enum {
    FLASH_STORE_HEADER_SIZE = 12,
    FLASH_STORE_CIPHER_PAD    = 3,  /* worst-case word-alignment slack */
};

static const uint32_t FLASH_STORE_MAGIC = 0x46534142u;

static void write_u32(uint8_t *destination, uint32_t value) {
    memcpy(destination, &value, sizeof(value));
}

static uint32_t read_u32(const uint8_t *source) {
    uint32_t value;
    memcpy(&value, source, sizeof(value));
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

static bool has_cipher(const FlashStore *store) {
    return store->config.encrypt != NULL
        && store->config.decrypt != NULL
        && store->config.cipher_key != NULL;
}

static size_t cipher_padded_size(size_t size) {
    return ((size + 3) / 4) * 4;
}

/* ---- page encode / decode -------------------------------------------- */

static void encode_page(FlashStore *store,
                        const uint8_t *data, size_t size) {
    uint8_t *page = store->config.workspace;
    memset(page, 0xFF, store->config.page_size);
    write_u32(page, FLASH_STORE_MAGIC);
    write_u32(page + 4, (uint32_t)size);
    write_u32(page + 8, crc32(data, size));

    if (has_cipher(store)) {
        size_t padded = cipher_padded_size(size);
        memcpy(page + FLASH_STORE_HEADER_SIZE, data, size);
        memset(page + FLASH_STORE_HEADER_SIZE + size, 0, padded - size);
        store->config.encrypt(page + FLASH_STORE_HEADER_SIZE,
                              padded, store->config.cipher_key);
    } else {
        memcpy(page + FLASH_STORE_HEADER_SIZE, data, size);
    }
}

static bool decode_page(FlashStore *store,
                        uint8_t *data, size_t size) {
    uint8_t *page = store->config.workspace;
    if (read_u32(page) != FLASH_STORE_MAGIC || read_u32(page + 4) != size) {
        return false;
    }

    if (has_cipher(store)) {
        size_t padded = cipher_padded_size(size);
        store->config.decrypt(page + FLASH_STORE_HEADER_SIZE,
                              padded, store->config.cipher_key);
        memcpy(data, page + FLASH_STORE_HEADER_SIZE, size);
    } else {
        memcpy(data, page + FLASH_STORE_HEADER_SIZE, size);
    }
    return read_u32(page + 8) == crc32(data, size);
}

/* ---- page I/O -------------------------------------------------------- */

static bool write_page(FlashStore *store, uint32_t address) {
    return store->config.io.erase(
               store->config.context, address, store->config.page_size) &&
           store->config.io.program(
               store->config.context, address, store->config.workspace,
               store->config.page_size);
}

/* ---- public API ------------------------------------------------------ */

FlashStore_Status FlashStore_Init(FlashStore *store,
                                  const FlashStore_Config *config) {
    if (store == NULL || config == NULL || config->io.read == NULL ||
        config->io.erase == NULL || config->io.program == NULL ||
        config->page_a_address == config->page_b_address ||
        config->page_size <= FLASH_STORE_HEADER_SIZE ||
        config->workspace == NULL) {
        return FLASH_STORE_ERROR_ARGUMENT;
    }

    store->config = *config;
    return FLASH_STORE_OK;
}

size_t FlashStore_MaxDataSize(const FlashStore *store) {
    if (store == NULL || store->config.page_size <= FLASH_STORE_HEADER_SIZE) {
        return 0;
    }
    size_t base = store->config.page_size - FLASH_STORE_HEADER_SIZE;
    if (has_cipher(store)) {
        base -= FLASH_STORE_CIPHER_PAD;
    }
    return base;
}

FlashStore_Status FlashStore_Save(FlashStore *store,
                                  const uint8_t *data, size_t size) {
    if (store == NULL || data == NULL ||
        size == 0 || size > FlashStore_MaxDataSize(store)) {
        return FLASH_STORE_ERROR_ARGUMENT;
    }

    encode_page(store, data, size);
    if (!write_page(store, store->config.page_a_address)) {
        return FLASH_STORE_ERROR_WRITE;
    }
    if (!write_page(store, store->config.page_b_address)) {
        return FLASH_STORE_ERROR_WRITE;
    }
    return FLASH_STORE_OK;
}

FlashStore_Status FlashStore_Load(FlashStore *store,
                                  uint8_t *data, size_t size) {
    if (store == NULL || data == NULL ||
        size == 0 || size > FlashStore_MaxDataSize(store)) {
        return FLASH_STORE_ERROR_ARGUMENT;
    }

    const uint32_t addresses[] = {
        store->config.page_a_address,
        store->config.page_b_address,
    };
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        if (store->config.io.read(
                store->config.context,
                addresses[i],
                store->config.workspace,
                store->config.page_size) &&
            decode_page(store, data, size)) {
            return FLASH_STORE_OK;
        }
    }
    return FLASH_STORE_ERROR_NO_VALID_DATA;
}
