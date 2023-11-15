/* A2D Electronics A2D_4CH_Isolated_ADC Board Library
 * Written By: Micah Black
 * Date: Nov 15, 2023
 *
 */

#include "A2D_4CH_Isolated_ADC.h"
#include "MCP3425.h"

//constructor and initialization list
A2D_4CH_Isolated_ADC::A2D_4CH_Isolated_ADC()
{	
	_adc_i2c_addrs[0] = A2D_4CH_ISO_ADC_CH1_I2C_ADDR;
	_adc_i2c_addrs[1] = A2D_4CH_ISO_ADC_CH2_I2C_ADDR;
	_adc_i2c_addrs[2] = A2D_4CH_ISO_ADC_CH3_I2C_ADDR;
	_adc_i2c_addrs[3] = A2D_4CH_ISO_ADC_CH4_I2C_ADDR;
	
	//EEPROM Addresses
	_ee_addr_initialized = 0;
	_ee_addr_serial = _ee_addr_initialized + sizeof(_ee_initialized);
	_ee_addr_v_scale[0] = _ee_addr_serial + sizeof(_serial);
	_ee_addr_v_off[0] = _ee_addr_v_scale[0] + sizeof(_v_scaling[0]);
	for(uint8_t i = 1; i < A2D_4CH_ISO_ADC_NUM_CHANNELS; i++)
	{
		_ee_addr_v_scale[i] = _ee_addr_v_off[i-1] + sizeof(_v_offset[i-1]);
		_ee_addr_v_off[i] = _ee_addr_v_scale[i] + sizeof(_v_scaling[i]);
	}
	
	//ADC 
	for(uint8_t i = 0; i < A2D_4CH_ISO_ADC_NUM_CHANNELS; i++)
	{
		_adc[i] = new MCP3425();
	}
}

void A2D_4CH_Isolated_ADC::init()
{
	pinMode(A2D_4CH_ISO_ADC_LED_PIN, OUTPUT);
	
	_init_cal_from_eeprom();
	
	for(uint8_t i = 0; i < A2D_4CH_ISO_ADC_NUM_CHANNELS; i++)
	{
		_adc[i]->init(_adc_i2c_addrs[i]);
		_adc[i]->reset();
		_adc[i]->set_gain(MCP3425_GAIN_1);
		_adc[i]->set_rate(MCP3425_DR_15SPS);
		_adc[i]->set_mode(MCP3425_MODE_CONTINUOUS);
	}
}

void A2D_4CH_Isolated_ADC::reset()
{
	set_led(false);
	
	for(uint8_t i = 0; i < A2D_4CH_ISO_ADC_NUM_CHANNELS; i++)
	{
		_adc[i]->init(_adc_i2c_addrs[i]);
	}
}

float A2D_4CH_Isolated_ADC::measure_raw_voltage(uint8_t ch)
{
	return _adc[ch]->measure_voltage_continuous();
}

float A2D_4CH_Isolated_ADC::measure_voltage(uint8_t ch)
{
	float voltage = _adc[ch]->measure_voltage_continuous();
	return _convert_adc_voltage_to_voltage(ch, voltage);
}

void A2D_4CH_Isolated_ADC::calibrate_voltage(uint8_t ch, float p1_meas, float p1_act, float p2_meas, float p2_act)
{
	//calculate new offset (b) and scaling (m) in:  actual = m * measured + b
	_v_scaling[ch] = (p2_act - p1_act) / (p2_meas - p1_meas); //rise in actual / run in measured
	_v_offset[ch] = p2_act - _v_scaling[ch] * p2_meas; //b = actual - m * measured
}

void A2D_4CH_Isolated_ADC::reset_calibration(uint8_t ch)
{
	//resets to the default calibration - assumes all components have 0% tolerance.
	_v_scaling[ch] = A2D_4CH_ISO_ADC_DEFAULT_V_SCALING;
	_v_offset[ch] = A2D_4CH_ISO_ADC_DEFAULT_V_OFFSET;
}

void A2D_4CH_Isolated_ADC::reset_all_calibration()
{
	for(uint8_t i = 0; i < A2D_4CH_ISO_ADC_NUM_CHANNELS; i++)
	{
		reset_calibration(i);
	}
}

void A2D_4CH_Isolated_ADC::save_calibration(uint8_t ch)
{
	EEPROM.put(_ee_addr_v_off[ch], _v_offset[ch]);
	EEPROM.put(_ee_addr_v_scale[ch], _v_scaling[ch]);
}

void A2D_4CH_Isolated_ADC::save_all_calibration()
{
	for(uint8_t i = 0; i < A2D_4CH_ISO_ADC_NUM_CHANNELS; i++)
	{
		save_calibration(i);
	}
	EEPROM.put(_ee_addr_initialized, A2D_4CH_ISO_ADC_EEPROM_INIT_VAL);
}

void A2D_4CH_Isolated_ADC::_init_cal_from_eeprom()
{
	//check the _ee_initialized byte
	EEPROM.get(_ee_addr_initialized, _ee_initialized);
	
	//if it is not correct, then load the default values in EEPROM
	if(_ee_initialized != A2D_4CH_ISO_ADC_EEPROM_INIT_VAL)
	{
		reset_all_calibration();
		save_all_calibration();
	}
	
	//now load the values from EEPROM to the class variables
	EEPROM.get(_ee_addr_serial, _serial);
	
	for(uint8_t i = 0; i < A2D_4CH_ISO_ADC_NUM_CHANNELS; i++)
	{
		EEPROM.get(_ee_addr_v_off[i], _v_offset[i]);
		EEPROM.get(_ee_addr_v_scale[i], _v_scaling[i]);
	}
}

void A2D_4CH_Isolated_ADC::set_led(bool state)
{
	digitalWrite(A2D_4CH_ISO_ADC_LED_PIN, state);
}

float A2D_4CH_Isolated_ADC::_convert_adc_voltage_to_voltage(uint8_t ch, float voltage)
{
	return (voltage - _v_offset[ch]) * _v_scaling[ch];
}
