#ifndef AIR001XX_HAL_H
#define AIR001XX_HAL_H

#include <stdint.h>

typedef enum {
    HAL_OK = 0,
    HAL_ERROR = 1
} HAL_StatusTypeDef;

typedef struct {
    uint32_t TypeErase;
    uint32_t PageAddress;
    uint32_t NbPages;
} FLASH_EraseInitTypeDef;

#define FLASH_TYPEERASE_PAGEERASE 0x02u
#define FLASH_TYPEPROGRAM_PAGE 0x01u

HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASH_Erase(FLASH_EraseInitTypeDef *erase, uint32_t *page_error);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address, uint32_t *data_address);

#endif
