#include "flash_store.h"

#include <string.h>

enum {
    FLASH_STORE_HEADER_SIZE = 12, /* magic(4) + len(4) + crc(4) */
};

static const uint32_t FLASH_STORE_MAGIC = 0x46534142u;

/* memcpy-based uint32 access — safe on alignment-restricted MCUs */
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

/*
 * Header layout (12 bytes):
 *   [0..3]  magic   — 0x46534142 ("FSAB")
 *   [4..7]  length  — payload size
 *   [8..11] crc32   — CRC32 of payload
 */

static FlashStore_Status save_page(const FlashStore_Config *config,
                                   uint32_t address,
                                   const uint8_t *data, size_t size) {
    uint8_t header[FLASH_STORE_HEADER_SIZE];
    write_u32(header,      FLASH_STORE_MAGIC);
    write_u32(header + 4,  (uint32_t)size);
    write_u32(header + 8,  crc32(data, size));

    if (!config->io->erase(config->io->context,
                           address, config->page_size))
        return FLASH_STORE_ERROR_WRITE;
    if (!config->io->program(config->io->context,
                             address, header, sizeof(header)))
        return FLASH_STORE_ERROR_WRITE;
    if (!config->io->program(config->io->context,
                             address + FLASH_STORE_HEADER_SIZE,
                             data, size))
        return FLASH_STORE_ERROR_WRITE;
    return FLASH_STORE_OK;
}

/*
 * Load a page.  On success, *stored_crc receives the CRC32 from the
 * header (so the caller can compare A vs B payloads).  stored_crc may
 * be NULL if the caller doesn't need it.
 */
static bool load_page(const FlashStore_Config *config, uint32_t address,
                      uint8_t *data, size_t size, uint32_t *stored_crc) {
    uint8_t header[FLASH_STORE_HEADER_SIZE];

    if (!config->io->read(config->io->context,
                          address, header, sizeof(header)))
        return false;
    if (read_u32(header) != FLASH_STORE_MAGIC)
        return false;
    if (read_u32(header + 4) != size)
        return false;
    if (stored_crc)
        *stored_crc = read_u32(header + 8);
    if (!config->io->read(config->io->context,
                          address + FLASH_STORE_HEADER_SIZE,
                          data, size))
        return false;
    return read_u32(header + 8) == crc32(data, size);
}

/* ---- public API ------------------------------------------------------ */

FlashStore_Status FlashStore_ConfigCheck(const FlashStore_Config *config) {
    if (config == NULL || config->io == NULL ||
        config->io->read == NULL ||
        config->io->erase == NULL ||
        config->io->program == NULL ||
        config->page_a_address == config->page_b_address ||
        config->page_size <= FLASH_STORE_HEADER_SIZE ||
        config->page_a_address % config->page_size != 0 ||
        config->page_b_address % config->page_size != 0) {
        return FLASH_STORE_ERROR_ARGUMENT;
    }

    /* check that the two pages don't overlap */
    uint32_t a_end = config->page_a_address + config->page_size;
    uint32_t b_end = config->page_b_address + config->page_size;
    if ((config->page_a_address < b_end &&
         config->page_b_address < a_end)) {
        return FLASH_STORE_ERROR_ARGUMENT;
    }

    return FLASH_STORE_OK;
}

size_t FlashStore_MaxDataSize(const FlashStore_Config *config) {
    if (config == NULL || config->page_size <= FLASH_STORE_HEADER_SIZE)
        return 0;
    return config->page_size - FLASH_STORE_HEADER_SIZE;
}

FlashStore_Status FlashStore_Save(const FlashStore_Config *config,
                                  const uint8_t *data, size_t size) {
    if (config == NULL || data == NULL ||
        size == 0 || size > FlashStore_MaxDataSize(config))
        return FLASH_STORE_ERROR_ARGUMENT;

    FlashStore_Status status = save_page(config, config->page_a_address,
                                         data, size);
    if (status != FLASH_STORE_OK) return status;

    return save_page(config, config->page_b_address, data, size);
}

FlashStore_Status FlashStore_Load(const FlashStore_Config *config,
                                  uint8_t *data, size_t size) {
    if (config == NULL || data == NULL ||
        size == 0 || size > FlashStore_MaxDataSize(config))
        return FLASH_STORE_ERROR_ARGUMENT;

    uint32_t a = config->page_a_address;
    uint32_t b = config->page_b_address;
    uint32_t crc_a = 0, crc_b = 0;
    bool a_ok = load_page(config, a, data, size, &crc_a);
    bool b_ok = load_page(config, b, data, size, &crc_b);

    if (!a_ok && !b_ok)
        return FLASH_STORE_ERROR_NO_VALID_DATA;

    if (a_ok && b_ok) {
        if (crc_a != crc_b) {
            /*
             * A and B hold different payloads.  A is always written first,
             * so A is newer.  B's load overwrote data — re-read A, then
             * repair B.
             */
            load_page(config, a, data, size, NULL);
            FlashStore_Status r = save_page(config, b, data, size);
            return (r == FLASH_STORE_OK) ? FLASH_STORE_OK
                                         : FLASH_STORE_WARN_REPAIR_FAILED;
        }
        /* CRC match — data is fine (A and B are identical), nothing to do. */
        return FLASH_STORE_OK;
    }

    /*
     * One page is corrupt.  If A is OK, re-read it (B's load overwrote
     * data).  If B is OK, data already holds B's payload — no re-read
     * needed.
     */
    FlashStore_Status repair;
    if (a_ok) {
        load_page(config, a, data, size, NULL);
        repair = save_page(config, b, data, size);
    } else {
        repair = save_page(config, a, data, size);
    }

    return (repair == FLASH_STORE_OK) ? FLASH_STORE_OK
                                      : FLASH_STORE_WARN_REPAIR_FAILED;
}
