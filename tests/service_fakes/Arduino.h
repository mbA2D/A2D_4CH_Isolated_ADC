#ifndef A2D_SERVICE_FAKE_ARDUINO_H
#define A2D_SERVICE_FAKE_ARDUINO_H
#include <stdint.h>
#include <string.h>
#define PC13 13
#define PB1 1
#define OUTPUT 1
#define HAL_OK 0
#define HAL_ERROR 1
#define FLASH_TYPEERASE_PAGES 1
#define FLASH_TYPEPROGRAM_HALFWORD 1
struct FLASH_EraseInitTypeDef {uint32_t TypeErase,PageAddress,NbPages;};
int HAL_FLASH_Unlock();
int HAL_FLASH_Lock();
int HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *,uint32_t *);
int HAL_FLASH_Program(uint32_t,uint32_t,uint64_t);
extern int pin_values[32];
extern unsigned long test_clock;
inline void pinMode(int,int) {}
inline void digitalWrite(int pin,int value) {pin_values[pin]=value;}
inline unsigned long millis() {return test_clock++;}
inline void delay(unsigned long ms) {test_clock+=ms;}
#endif
