#include "flash_store.h"
#include "xxtea.h"

#include "air001xx_hal.h"

#include <stdint.h>
#include <string.h>

enum {
    FLASH_PAGE_SIZE = 128,
    FLASH_TOTAL_SIZE = 32 * 1024,
};

#define FLASH_BASE_ADDRESS 0x08000000u
#define FLASH_PAGE_A_ADDRESS \
    (FLASH_BASE_ADDRESS + FLASH_TOTAL_SIZE - FLASH_PAGE_SIZE)
#define FLASH_PAGE_B_ADDRESS \
    (FLASH_BASE_ADDRESS + FLASH_TOTAL_SIZE - 2u * FLASH_PAGE_SIZE)

static bool air001_read(void *context, uint32_t address,
                        uint8_t *output, size_t length) {
    (void)context;
    memcpy(output, (const void *)(uintptr_t)address, length);
    return true;
}

static bool air001_erase(void *context, uint32_t address, size_t length) {
    (void)context;
    if (length != FLASH_PAGE_SIZE) return false;

    FLASH_EraseInitTypeDef erase = {
        .TypeErase  = FLASH_TYPEERASE_PAGEERASE,
        .PageAddress = address,
        .NbPages    = 1,
    };
    uint32_t page_error = 0;
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef status = HAL_FLASH_Erase(&erase, &page_error);
    HAL_FLASH_Lock();
    return status == HAL_OK;
}

static bool air001_program(void *context, uint32_t address,
                           const uint8_t *data, size_t length) {
    (void)context;
    (void)length;  /* page-level program writes a full page */
    /*
     * Production code may program word-by-word with FLASH_TYPEPROGRAM_WORD.
     * This reference implementation uses page-level program for simplicity;
     * the caller must ensure length ≤ page_size and address is page-aligned
     * when using TYPEERASE_PAGE.
     */
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef status = HAL_FLASH_Program(
        FLASH_TYPEPROGRAM_PAGE, address, (uint32_t *)(void *)data);
    HAL_FLASH_Lock();
    return status == HAL_OK;
}

typedef struct {
    uint16_t report_interval_minutes;
    uint8_t  brightness;
    uint8_t  flags;
} AppSettings;

/*
 * Encryption is handled OUTSIDE flash_store — encrypt before Save,
 * decrypt after Load.  We use the provided workspace buffer as scratch
 * space for XXTEA (which needs word-aligned, ≥8-byte input).
 */
static void encrypt_settings(uint8_t *workspace,
                             AppSettings *s, const uint32_t key[4]) {
    size_t padded = ((sizeof(*s) + 3) / 4) * 4;
    if (padded < 8) padded = 8;
    memcpy(workspace, s, sizeof(*s));
    memset(workspace + sizeof(*s), 0, padded - sizeof(*s));
    xxtea_encrypt(workspace, padded, key);
    memcpy(s, workspace, padded);   /* copy ciphertext back */
}

static void decrypt_settings(uint8_t *workspace,
                             AppSettings *s, const uint32_t key[4]) {
    size_t padded = ((sizeof(*s) + 3) / 4) * 4;
    if (padded < 8) padded = 8;
    memcpy(workspace, s, padded);
    xxtea_decrypt(workspace, padded, key);
    memcpy(s, workspace, sizeof(*s));  /* copy plaintext back */
}

void app_settings_example(void) {
    const uint32_t key[4] = {
        0x12345678, 0x9ABCDEF0, 0x0FEDCBA9, 0x87654321
    };
    /* XXTEA scratch buffer — only needed if you use encryption */
    _Alignas(uint32_t) static uint8_t cipher_workspace[128];

    FlashStore store;
    FlashStore_Config config = {
        .io = {
            .read   = air001_read,
            .erase  = air001_erase,
            .program = air001_program,
        },
        .page_a_address = FLASH_PAGE_A_ADDRESS,
        .page_b_address = FLASH_PAGE_B_ADDRESS,
        .page_size      = FLASH_PAGE_SIZE,
    };

    AppSettings settings = {
        .report_interval_minutes = 30,
        .brightness = 60,
        .flags      = 1,
    };

    if (FlashStore_Init(&store, &config) != FLASH_STORE_OK) return;

    /* save: encrypt → store */
    encrypt_settings(cipher_workspace, &settings, key);
    (void)FlashStore_Save(&store, (const uint8_t *)&settings,
                          sizeof(settings));

    /* load: load → decrypt */
    (void)FlashStore_Load(&store, (uint8_t *)&settings,
                          sizeof(settings));
    decrypt_settings(cipher_workspace, &settings, key);
}
