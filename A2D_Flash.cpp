#include "A2D_Flash.h"
#include <Arduino.h>
#include <string.h>
namespace a2d {
void readSettingsPage(uint8_t page, void *data, size_t size) {
    if (page < 2 && size <= SettingsPageSize)
        memcpy(data, reinterpret_cast<const void *>(SettingsFlashBase + page * SettingsPageSize), size);
}
bool writeSettingsPage(uint8_t page, const void *data, size_t size) {
    if (page >= 2 || size < 4 || size > SettingsPageSize || size % 4) return false;
    if (HAL_FLASH_Unlock() != HAL_OK) return false;
    FLASH_EraseInitTypeDef erase = {};
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = SettingsFlashBase + page * SettingsPageSize;
    erase.NbPages = 1;
    uint32_t error = 0;
    bool ok = HAL_FLASHEx_Erase(&erase, &error) == HAL_OK;
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    // The record's commit word is last. A partial halfword/word never commits a record.
    for (size_t offset = 0; ok && offset < size; offset += 2) {
        const uint16_t value = static_cast<uint16_t>(bytes[offset]) | (static_cast<uint16_t>(bytes[offset + 1]) << 8);
        ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, erase.PageAddress + offset, value) == HAL_OK;
    }
    HAL_FLASH_Lock();
    return ok && !memcmp(reinterpret_cast<const void *>(erase.PageAddress), data, size);
}
}
