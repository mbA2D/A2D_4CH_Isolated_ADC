/*
AUTHOR: Micah Black, A2D Electronics
DATE: Nov 16, 2023
PURPOSE: This example implements some SCPI commands
		(that don't completely follow the susbystem style standard)
		to communicate with the 4ch ADC board.
CHANGELOG:

*/

#include <A2D_4CH_Isolated_ADC.h>

#define MANUFACTURER ("A2D Electronics")
#define MODEL		 ("4CH Isolated ADC")
#define SERIAL_NUM   ("00001")
#define VERSION      ("V1.0.0")

//SERIAL DEFINES
#define BAUDRATE      115200
#define SER_BUF_LEN   256
#define CMD_BUF_LEN   32
#define END_CHAR      '\n'
#define NO_CMD        ""

//Macro for finding commands - F to store string literal
//in flash instead of memory
#define CMDIS(i,c) (!strcmp_P(i, PSTR(c)))

//Function Prototypes:
void parse_serial(char ser_buf[], char command[], char* token);
uint8_t parse_channel(char* token);

A2D_4CH_Isolated_ADC adc;

void setup() {
  // put your setup code here, to run once:
  Wire.begin();
  Serial.begin(BAUDRATE);
  adc.init();
}

void loop() {

  //Allocate memory for the serial buffer
  char ser_buf[SER_BUF_LEN];
  char command[CMD_BUF_LEN];
  char* token;
  uint8_t chars_input = 0;
  
  //if serial data is available
  if(Serial.available()){
	//Read until a full command is received
	chars_input = Serial.readBytesUntil(END_CHAR, ser_buf, SER_BUF_LEN);
	//terminate the input string with NULL
	ser_buf[chars_input] = '\0';
	parse_serial(ser_buf, command, token);
  }
  else{
	strcpy(command, "NOCMD");
  }

  //NOCMD?
  if(CMDIS(command, "NOCMD")){
	;
  }
  
  //*IDN?
  else if(CMDIS(command, "*IDN?")){
	Serial.print(MANUFACTURER);
	Serial.print(",");
	Serial.print(MODEL);
	Serial.print(",");
	Serial.print(SERIAL_NUM);
	Serial.print(",");
	Serial.println(VERSION);
	Serial.flush();
  }
  
  //*RST
  else if (CMDIS(command, "*RST")){
	adc.reset();
  }
  
  //*CLS
  else if (CMDIS(command, "*CLS")){
	; //nothing since we don't have errors yet
  }
  
  //INSTR:LED VAL
  //VAL is boolean 0 or 1
  else if (CMDIS(command, "INSTR:LED")){
	char delimeters[] = " ,";
	token = strtok(NULL, delimeters);
	adc.set_led(bool(atoi(token)));
  }

  //MEAS:VOLT CH?
  //CH is integer in range of 1-4, or 0. 0 means read all 4 channels
  else if (CMDIS(command, "MEAS:VOLT")){
	uint8_t ch = parse_channel(token);
	if(ch == 0)
	{
		for(uint8_t i = 0; i < A2D_4CH_ISO_ADC_NUM_CHANNELS; i++)
		{
			Serial.print(adc.measure_voltage(i), 4);
			if(i != A2D_4CH_ISO_ADC_NUM_CHANNELS-1)
			{
				Serial.print(",");
			}
		}
		Serial.println("");
	}
	else if(ch >= 1 && ch <= A2D_4CH_ISO_ADC_NUM_CHANNELS)
	{
		Serial.println(adc.measure_voltage(ch-1), 4);
	}
	Serial.flush();
  }
  
  //MEAS:VOLT:ADC CH?
  //CH is integer in range of 1-4, or 0. 0 means read all 4 channels
  else if (CMDIS(command, "MEAS:VOLT:ADC")){
	uint8_t ch = parse_channel(token);
	if(ch == 0)
	{
		for(uint8_t i = 0; i < A2D_4CH_ISO_ADC_NUM_CHANNELS; i++)
		{
			Serial.print(adc.measure_raw_voltage(i), 4);
			if(i != A2D_4CH_ISO_ADC_NUM_CHANNELS-1)
			{
				Serial.print(",");
			}
		}
		Serial.println("");
	}
	else if(ch >= 1 && ch <= A2D_4CH_ISO_ADC_NUM_CHANNELS)
	{
		Serial.println(adc.measure_raw_voltage(ch-1), 4);
	}
	Serial.flush();
  }
  
  //CAL:RESET CH?
  //CH is integer in range of 1-4, or 0. 0 means reset all 4 channels
  else if (CMDIS(command, "CAL:RESET")){
	  uint8_t ch = parse_channel(token);
	  if(ch == 0)
	  {
		adc.reset_all_calibration();
	  }
	  else if(ch >= 1 && ch <= A2D_4CH_ISO_ADC_NUM_CHANNELS)
	  {
		adc.reset_calibration(ch-1);
	  }
  }
  
  //CAL:SAVE CH?
  //CH is integer in range of 1-4, or 0. 0 means save all 4 channels
  else if (CMDIS(command, "CAL:SAVE")){
	  uint8_t ch = parse_channel(token);
	  if(ch == 0)
	  {
		adc.save_all_calibration();
	  }
	  else if(ch >= 1 && ch <= A2D_4CH_ISO_ADC_NUM_CHANNELS)
	  {
		adc.save_calibration(ch-1);
	  }
  }
  
  //CAL:VOLT MEAS1,ACTUAL1,MEAS2,ACTUAL2,CH
  else if (CMDIS(command, "CAL:VOLT")){
	//parse all the floats
	char delimeters[] = " ,";
	
	const uint8_t num_floats = 4;
	//create memory to store all the floats
	float float_arr[num_floats];
	for (uint8_t index = 0; index < num_floats; index++)
	{
		token = strtok(NULL, delimeters);
		char str_float[32];
		strcpy(str_float, token);
		float_arr[index] = String(str_float).toFloat();
	}
	
	uint8_t ch = parse_channel(token);
	if(ch >= 1 && ch <= A2D_4CH_ISO_ADC_NUM_CHANNELS)
	{
		adc.calibrate_voltage(ch-1, float_arr[0], float_arr[1], float_arr[2], float_arr[3]);
	}
  }
  
  //CAL CH?
  //CH is integer in range of 1-4, or 0. 0 means return all 4 channels
  else if (CMDIS(command, "CAL")){
	  uint8_t ch = parse_channel(token);
	  if(ch == 0)
	  {
		for(uint8_t i = 0; i < A2D_4CH_ISO_ADC_NUM_CHANNELS; i++)
		{
			Serial.print(adc.get_cal_offset(i), 5);
			Serial.print(",");
			Serial.print(adc.get_cal_gain(i), 5);
			if(i != A2D_4CH_ISO_ADC_NUM_CHANNELS-1)
			{
				Serial.print(",");
			}
		}
		Serial.println("");
	  }
	  else if(ch >= 1 && ch <= A2D_4CH_ISO_ADC_NUM_CHANNELS)
	  {
		Serial.print(adc.get_cal_offset(ch-1), 5);
		Serial.print(",");
		Serial.println(adc.get_cal_gain(ch-1), 5);
	  }
	  Serial.flush();
  }

}

uint8_t parse_channel(char* token)
{
	char delimeters[] = " ,?";
	token = strtok(NULL, delimeters);
	uint8_t channel = uint8_t(atoi(token));
	if(channel >= 0 && channel <= A2D_4CH_ISO_ADC_NUM_CHANNELS)
	{
		return channel;
	}
	else
	{
		return A2D_4CH_ISO_ADC_NUM_CHANNELS + 1;
	}
}

void parse_serial(char ser_buf[], char command[], char* token)
{  
  //we will assume only 1 command is sent at a time
  //so we don't have to deal with SCPI's ';' to send
  //multiple commands on the same line
  
  //split input string on space to extract the command and the parameters
  //strtok replaces the delimeter with NULL to terminate the string
  //strtok maintains a static pointer to the original string passed to it.
  //to get the next token, pass NULL as the first argument.
  
  char delimeters[] = " ";
  
  token = strtok(ser_buf, delimeters);
  strcpy(command, token); //copies the token into command.
  
}
