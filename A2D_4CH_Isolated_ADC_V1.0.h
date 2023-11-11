//Board Description File for A2D_Sense_Board_V1.0 Board

#include "MCP3425.h"
#include "Arduino.h"

//Valid I2C Address
#define A2D_4CH_ISO_ADC_CH1_I2C_ADDR	0x68
#define A2D_4CH_ISO_ADC_CH2_I2C_ADDR	0x69
#define A2D_4CH_ISO_ADC_CH3_I2C_ADDR	0x6A
#define A2D_4CH_ISO_ADC_CH4_I2C_ADDR	0x6B

#define A2D_4CH_ISO_ADC_LED_PIN         PC13

//scaling
#define A2D_4CH_ISO_ADC_V_REF			2.048

#define A2D_4CH_ISO_ADC_DEFAULT_V_SCALING       	10.35 // (18.7k + 2k)/2k
#define A2D_4CH_ISO_ADC_DEFAULT_V_OFFSET	0
