#ifndef A2D_FLASH_H
#define A2D_FLASH_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define A2D_SETTINGS_FLASH_BASE 0x0800F800UL
#define A2D_SETTINGS_PAGE_SIZE 1024U
void a2d_flash_read_page(uint8_t page, void *data, size_t size);
bool a2d_flash_write_page(uint8_t page, const void *data, size_t size);

#ifdef __cplusplus
}
#endif
#endif
