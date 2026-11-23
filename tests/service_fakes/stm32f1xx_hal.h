#ifndef A2D_SERVICE_FAKE_STM32F1XX_HAL_H
#define A2D_SERVICE_FAKE_STM32F1XX_HAL_H
#include <stdint.h>
#define HAL_OK 0
#define HAL_ERROR 1
#define FLASH_TYPEERASE_PAGES 1
#define FLASH_TYPEPROGRAM_HALFWORD 1
typedef struct { uint32_t TypeErase, PageAddress, NbPages; } FLASH_EraseInitTypeDef;
#ifdef __cplusplus
extern "C" {
#endif
int HAL_FLASH_Unlock(void);
int HAL_FLASH_Lock(void);
int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *erase, uint32_t *error);
int HAL_FLASH_Program(uint32_t type, uint32_t address, uint64_t value);
#ifdef __cplusplus
}
#endif
#endif
