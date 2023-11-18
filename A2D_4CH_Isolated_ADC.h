/* A2D Electronics A2D_4CH_Isolated_ADC Board Library
 * Written By: Micah Black
 * Date: Nov 15, 2023
 * 
 */


#ifndef A2D_4CH_Isolated_ADC_h
#define A2D_4CH_Isolated_ADC_h

#include <Arduino.h>
#include <Wire.h>
#include "A2D_4CH_Isolated_ADC_V1.0.h" //header file with pins, etc
#include "MCP3425.h"
#include <EEPROM.h>

class A2D_4CH_Isolated_ADC
{
	public:
		A2D_4CH_Isolated_ADC(); //constructor
		
		//scaling for voltages
		float _v_scaling[A2D_4CH_ISO_ADC_NUM_CHANNELS]; // V/V
		float _v_offset[A2D_4CH_ISO_ADC_NUM_CHANNELS]; //V
		
		//Configuration
		void init();
		void reset();
		
		//Interface
		float measure_voltage(uint8_t ch);
		float measure_raw_voltage(uint8_t ch);
		void calibrate_voltage(uint8_t ch, float p1_meas, float p1_act, float p2_meas, float p2_act);
		void reset_calibration(uint8_t ch);
		void reset_all_calibration();
		void save_calibration(uint8_t ch);
		void save_all_calibration();
		void set_led(bool state);
		float get_cal_offset(uint8_t ch);
		float get_cal_gain(uint8_t ch);

	private:
		//************METHODS****************
		float _convert_adc_voltage_to_voltage(uint8_t ch, float voltage);
		void _init_cal_from_eeprom();
		
		//*********VARIABLES/CLASSES*********
		uint8_t _ee_initialized;
		uint32_t _serial;
		
		//EEPROM Addresses
		int _ee_addr_initialized;
		int _ee_addr_serial;
		int _ee_addr_v_off[A2D_4CH_ISO_ADC_NUM_CHANNELS];
		int _ee_addr_v_scale[A2D_4CH_ISO_ADC_NUM_CHANNELS];
		
		//ADC
		uint8_t _adc_i2c_addrs[A2D_4CH_ISO_ADC_NUM_CHANNELS];
		MCP3425* _adc[A2D_4CH_ISO_ADC_NUM_CHANNELS];
};

#endif