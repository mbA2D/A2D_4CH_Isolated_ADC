//Board Description File for A2D_Sense_Board_V1.0 Board

#include "MCP3425.h"
#include "Arduino.h"

//Valid I2C Address
#define A2D_4CH_ISO_ADC_CH1_I2C_ADDR	0x68
#define A2D_4CH_ISO_ADC_CH2_I2C_ADDR	0x69
#define A2D_4CH_ISO_ADC_CH3_I2C_ADDR	0x6A
#define A2D_4CH_ISO_ADC_CH4_I2C_ADDR	0x6B

#define A2D_4CH_ISO_ADC_LED_PIN         PC13
#define A2D_4CH_ISO_ADC_LED_ON          0
#define A2D_4CH_ISO_ADC_LED_OFF         1

#define A2D_4CH_ISO_ADC_NUM_CHANNELS	4

//scaling
#define A2D_4CH_ISO_ADC_DEFAULT_V_SCALING   2.818181818 // (1.1k + 2k)/1.1k
#define A2D_4CH_ISO_ADC_DEFAULT_V_OFFSET	0

#define A2D_4CH_ISO_ADC_EEPROM_INIT_VAL		0x55 //0b01010101
