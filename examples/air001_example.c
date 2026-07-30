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
#define FLASH_PAGE_A_ADDRESS (FLASH_BASE_ADDRESS + FLASH_TOTAL_SIZE - FLASH_PAGE_SIZE)
#define FLASH_PAGE_B_ADDRESS (FLASH_BASE_ADDRESS + FLASH_TOTAL_SIZE - 2u * FLASH_PAGE_SIZE)

_Alignas(uint32_t) static uint8_t workspace[FLASH_PAGE_SIZE];

/* 128-bit XXTEA key — replace with your own */
static const uint32_t cipher_key[4] = {
    0x12345678u, 0x9ABCDEF0u, 0x0FEDCBA9u, 0x87654321u
};

static bool air001_read(void *context, uint32_t address, uint8_t *output, size_t length) {
    (void)context;
    memcpy(output, (const void *)(uintptr_t)address, length);
    return true;
}

static bool air001_erase(void *context, uint32_t address, size_t length) {
    (void)context;
    if (length != FLASH_PAGE_SIZE) {
        return false;
    }

    FLASH_EraseInitTypeDef erase = {
        .TypeErase = FLASH_TYPEERASE_PAGEERASE,
        .PageAddress = address,
        .NbPages = 1,
    };
    uint32_t page_error = 0;
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef status = HAL_FLASH_Erase(&erase, &page_error);
    HAL_FLASH_Lock();
    return status == HAL_OK;
}

static bool air001_program(
    void *context,
    uint32_t address,
    const uint8_t *data,
    size_t length
) {
    (void)context;
    if (length != FLASH_PAGE_SIZE) {
        return false;
    }

    HAL_FLASH_Unlock();
    HAL_StatusTypeDef status = HAL_FLASH_Program(
        FLASH_TYPEPROGRAM_PAGE,
        address,
        (uint32_t *)(void *)data
    );
    HAL_FLASH_Lock();
    return status == HAL_OK;
}

typedef struct {
    uint16_t report_interval_minutes;
    uint8_t brightness;
    uint8_t flags;
} AppSettings;

void app_settings_example(void) {
    FlashStore store;
    FlashStore_Config config = {
        .io = {
            .read = air001_read,
            .erase = air001_erase,
            .program = air001_program,
        },
        .page_a_address = FLASH_PAGE_A_ADDRESS,
        .page_b_address = FLASH_PAGE_B_ADDRESS,
        .page_size = FLASH_PAGE_SIZE,
        .workspace = workspace,
        .encrypt    = xxtea_encrypt,
        .decrypt    = xxtea_decrypt,
        .cipher_key = cipher_key,
    };

    AppSettings settings = {
        .report_interval_minutes = 30,
        .brightness = 60,
        .flags = 1,
    };

    if (FlashStore_Init(&store, &config) != FLASH_STORE_OK) {
        return;
    }

    (void)FlashStore_Save(&store, (const uint8_t *)&settings, sizeof(settings));
    (void)FlashStore_Load(&store, (uint8_t *)&settings, sizeof(settings));
}
