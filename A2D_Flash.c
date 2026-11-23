#include "A2D_Flash.h"
#include <stm32f1xx_hal.h>
#include <string.h>
void a2d_flash_read_page(uint8_t page, void *data, size_t size) {
    if (page < 2 && size <= A2D_SETTINGS_PAGE_SIZE)
        memcpy(data, (const void *)(A2D_SETTINGS_FLASH_BASE + page * A2D_SETTINGS_PAGE_SIZE), size);
}
bool a2d_flash_write_page(uint8_t page, const void *data, size_t size) {
    if (page >= 2 || size < 4 || size > A2D_SETTINGS_PAGE_SIZE || size % 4) return false;
    if (HAL_FLASH_Unlock() != HAL_OK) return false;
    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = A2D_SETTINGS_FLASH_BASE + page * A2D_SETTINGS_PAGE_SIZE;
    erase.NbPages = 1;
    uint32_t error = 0;
    bool ok = HAL_FLASHEx_Erase(&erase, &error) == HAL_OK;
    const uint8_t *bytes = data;
    /* Commit word is last: a partial halfword or word cannot commit a record. */
    for (size_t offset = 0; ok && offset < size; offset += 2) {
        const uint16_t value = (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1] << 8);
        ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, erase.PageAddress + offset, value) == HAL_OK;
    }
    HAL_FLASH_Lock();
    return ok && !memcmp((const void *)erase.PageAddress, data, size);
}
