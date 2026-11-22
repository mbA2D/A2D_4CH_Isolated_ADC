#ifndef A2D_FLASH_H
#define A2D_FLASH_H
#include <stdint.h>
#include <stddef.h>
namespace a2d {
// Two 1KiB pages, outside the bootloader and application. Each save targets the inactive page.
constexpr uint32_t SettingsFlashBase = 0x0800f800UL;
constexpr size_t SettingsPageSize = 1024;
void readSettingsPage(uint8_t page, void *data, size_t size);
bool writeSettingsPage(uint8_t page, const void *data, size_t size);
}
#endif
