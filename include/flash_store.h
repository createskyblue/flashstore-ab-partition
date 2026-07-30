#ifndef FLASH_STORE_H
#define FLASH_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    FLASH_STORE_OK = 0,
    FLASH_STORE_ERROR_ARGUMENT,
    FLASH_STORE_ERROR_READ,
    FLASH_STORE_ERROR_WRITE,
    FLASH_STORE_ERROR_NO_VALID_DATA
} FlashStore_Status;

typedef struct {
    bool (*read)(void *context, uint32_t address, uint8_t *output, size_t length);
    bool (*erase)(void *context, uint32_t address, size_t length);
    bool (*program)(void *context, uint32_t address, const uint8_t *data, size_t length);
} FlashStore_IO;

/* Cipher plug-in.  Set both `encrypt` and `decrypt` (or neither — NULL
 * means plaintext storage).  The cipher key is a separate pointer so the
 * store never owns it; the caller keeps the key alive. */
typedef void (*FlashStore_Cipher)(uint8_t *data, size_t size,
                                  const uint32_t key[4]);

typedef struct {
    FlashStore_IO      io;
    void              *context;
    uint32_t           page_a_address;
    uint32_t           page_b_address;
    size_t             page_size;
    uint8_t           *workspace;   /* must be >= page_size bytes */
    FlashStore_Cipher  encrypt;
    FlashStore_Cipher  decrypt;
    const uint32_t    *cipher_key;
} FlashStore_Config;

typedef struct {
    FlashStore_Config config;
} FlashStore;

FlashStore_Status FlashStore_Init(FlashStore *store,
                                  const FlashStore_Config *config);
FlashStore_Status FlashStore_Save(FlashStore *store,
                                  const uint8_t *data, size_t size);
FlashStore_Status FlashStore_Load(FlashStore *store,
                                  uint8_t *data, size_t size);
size_t FlashStore_MaxDataSize(const FlashStore *store);

#endif
