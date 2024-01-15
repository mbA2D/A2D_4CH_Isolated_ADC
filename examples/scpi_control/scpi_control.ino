/*
AUTHOR: Micah Black, A2D Electronics
DATE: Nov 16, 2023
PURPOSE: This example implements some SCPI commands
		(that don't completely follow the susbystem style standard)
		to communicate with the 4ch ADC board.
CHANGELOG:
	Jan 14, 2024: V1.0.1: Added RS485 communication with MEAS:VOLT command
*/

#include <A2D_4CH_Isolated_ADC.h>

//#define DEBUG 1 //uncomment this line to add all debug printing statements
#define FORCE_EEPROM_REINIT 1 //uncomment this line to force reinitialization of EEPROM (serial number, calibration, RS485 Address, etc.)

#define MANUFACTURER ("A2D Electronics")
#define MODEL		 ("4CH Isolated ADC")
#define SERIAL_NUM   ("0009")
#define VERSION      ("V1.0.1")

//SERIAL DEFINES
#define BAUDRATE      115200
#define SER_BUF_LEN   256
#define CMD_BUF_LEN   32
#define END_CHAR      '\n'
#define NO_CMD        ""

//RS485 Settings
#define RS485_BAUDRATE	57600

//Macro for finding commands - F to store string literal
//in flash instead of memory
#define CMDIS(i,c) (!strcmp_P(i, PSTR(c)))

//Function Prototypes:
uint8_t parse_uint8_t(char* token);
uint8_t parse_channel(char* token);
bool channel_on_this_device(uint8_t channel);
void parse_serial(char ser_buf[], char command[], char* token);
uint8_t parse_serial_2(char ser_buf[], char command[], char* token);
void wait_rs485_response_send_usb(char ser_buf[]);


A2D_4CH_Isolated_ADC adc;
HardwareSerial Serial3(PB11, PB10); //RX, TX - for RS485

void setup() {
  // put your setup code here, to run once:
  Wire.begin(); //I2C
  Serial.begin(BAUDRATE); //Communication with PC over USB
  Serial3.begin(RS485_BAUDRATE); //RS485 Communication
  adc.init();
  
  #ifdef FORCE_EEPROM_REINIT
  adc.force_eeprom_reinit();
  #endif
}

void loop() {

  //Allocate memory for the serial buffer
  char ser_buf[SER_BUF_LEN];
  char command[CMD_BUF_LEN];
  char* token;
  uint8_t chars_input = 0;
  uint8_t cmd_source = 0;
  
  //TODO
  //If the command is not for this device, then we need to send it to the correct one (RS485 Address)
  //Then wait for a response (with timeout if the device does not respond).
  //RS485 address needs to be stored in EEPROM (DONE)
  //Address of 0 is reserved for the base device (the one connected with USB)
  //Commands with '0' as the channel will only return the 4 channels of the base device.
  
  //TODO - add a CRC check to RS485 communication.
  //The same firmware should be uploaded to the base device as any others.
  //If channel is greater than 4, then use [CH - 4*RS485_ADDR] as the address.
  
  //CHECK COMMANDS FROM USB (priority 1)
  if(Serial.available()){
	#ifdef DEBUG
	Serial.println(F("USB Available"));
	#endif
	
	//Read until a full command is received
	chars_input = Serial.readBytesUntil(END_CHAR, ser_buf, SER_BUF_LEN);
	ser_buf[chars_input] = '\0'; //terminate the input string with NULL to work with strtok
	parse_serial(ser_buf, command, token);
	
	cmd_source = A2D_4CH_ISO_ADC_CMD_SOURCE_USB;
  }
  //CHECK COMMANDS FROM RS485 (priority 2)
  else if (Serial3.available()){
	#ifdef DEBUG
	Serial.println(F("RS485 Available"));
	#endif
	
	chars_input = Serial3.readBytesUntil(END_CHAR, ser_buf, SER_BUF_LEN);
	#ifdef DEBUG
	Serial.print(F("Chars read: "));
	Serial.println(chars_input);
	#endif
	ser_buf[chars_input] = '\0'; //terminate the input string with NULL to work with strtok
	#ifdef DEBUG
	Serial.println(ser_buf);
	#endif
	
	//if from RS485, this could be a response a device or a command to a device.
	//Command out from base: rs485_addr is 1 to 32
	//Command response to base: rs485_addr is 0
	
	
	//if addr is 0 and this is the base device, then send the rest of the response out over USB.
	uint8_t rs485_address = parse_serial_2(ser_buf, command, token);
	#ifdef DEBUG
	Serial.print(F("RS485 Address read: "));
	Serial.println(rs485_address);
	#endif
	
	//if address is for this device
	if(rs485_address == adc.get_rs485_addr()){
		//if this is the base device, print over USB - this is a response from another device
		if(adc.get_rs485_addr() == A2D_4CH_ISO_ADC_BASE_RS485_ADDR){
			Serial.print(command); //No extra /n needed. Responses will not have spaces, so the command delimeter will not get a partial response.
			Serial.flush();
			strcpy(command, "NOCMD"); //don't do anything else
		}
		else{
			//we need to process the command, but we send the response over RS485 instead of USB.
			#ifdef DEBUG
			Serial.println(F("cmd_source = RS485"));
			#endif
			cmd_source = A2D_4CH_ISO_ADC_CMD_SOURCE_RS485;
		}	
	}
	
  }
  else{
	strcpy(command, "NOCMD");
  }

  //NOCMD
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
  else if (CMDIS(command, "*RST"))
  {
	adc.reset();
  }
  
  //*CLS
  else if (CMDIS(command, "*CLS"))
  {
	; //nothing since we don't have errors yet
  }
  
  //*TRG
  else if (CMDIS(command, "*TRG"))
  {
    adc.trigger_all_single_shot();
  }
  
  //INSTR:LED VAL
  //VAL is boolean 0 or 1
  else if (CMDIS(command, "INSTR:LED"))
  {
	char delimeters[] = " ,";
	token = strtok(NULL, delimeters);
	adc.set_led(bool(atoi(token)));
  }

  //MEAS:VOLT CH?
  //CH is integer in range of 1-4, or 0. 0 means read all 4 channels
  else if (CMDIS(command, "MEAS:VOLT"))
  {
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
		Serial.flush();
	}
	else if(ch >= 1 && ch <= A2D_4CH_ISO_ADC_NUM_CHANNELS)
	{
		if(cmd_source == A2D_4CH_ISO_ADC_CMD_SOURCE_USB)
		{
			Serial.println(adc.measure_voltage(ch-1), 4);
			Serial.flush();
		}
		else if(cmd_source == A2D_4CH_ISO_ADC_CMD_SOURCE_RS485)
		{
			#ifdef DEBUG
			Serial.println(F("Command from RS485"));
			#endif
			adc.set_rs485_receive(false);
			Serial3.println(adc.measure_voltage(ch-1), 4);
			Serial3.flush();
			adc.set_rs485_receive(true);
			#ifdef DEBUG
			Serial.println(F("Response sent RS485"));
			#endif
		}
	}
	else if(adc.get_rs485_addr() == A2D_4CH_ISO_ADC_BASE_RS485_ADDR) //channel is not 0, and channel is not on this device, and this is a base device.
	{  
		//if this is a base device, send command up over RS485.
		//  calculate the correct RS485 addr to use - I need the channel for this.
		adc.set_rs485_receive(false);
		Serial3.print(uint8_t((ch-1)/A2D_4CH_ISO_ADC_NUM_CHANNELS));//rs485 address
		Serial3.print(" ");
		Serial3.print(command);
		Serial3.print(" ");
		Serial3.print(ch);
		Serial3.println("?");
		Serial3.flush();
		adc.set_rs485_receive(true);
		
		#ifdef DEBUG
		Serial.print(F("Printed over RS485: "));
		Serial.print(uint8_t((ch-1)/A2D_4CH_ISO_ADC_NUM_CHANNELS));//destination rs485 address
		Serial.print(" ");
		Serial.print(command);
		Serial.print(" ");
		Serial.print(ch);
		Serial.println("?");
		Serial.flush();
		#endif
		
		wait_rs485_response_send_usb(ser_buf);
	}
	
  }
  
  //MEAS:VOLT:ADC CH?
  //CH is integer in range of 1-4, or 0. 0 means read all 4 channels
  else if (CMDIS(command, "MEAS:VOLT:ADC"))
  {
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
  
  //CAL:RESET CH
  //CH is integer in range of 1-4, or 0. 0 means reset all 4 channels
  else if (CMDIS(command, "CAL:RESET"))
  {
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
  
  //CAL:SAVE CH
  //CH is integer in range of 1-4, or 0. 0 means save all 4 channels
  else if (CMDIS(command, "CAL:SAVE"))
  {
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
  else if (CMDIS(command, "CAL:VOLT"))
  {
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
  else if (CMDIS(command, "CAL"))
  {
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

 //INSTR:GAIN GAIN,CH
 //GAIN 1,2,4,8
 //CH is integer in range of 1-4, or 0. 0 means all 4 channels
  else if (CMDIS(command, "INSTR:GAIN"))
  {
	  uint8_t gain_p = parse_uint8_t(token);
	  uint8_t gain = 0;
	  uint8_t ch = parse_channel(token);
	  
	  bool valid_gain = false;
	  if(gain_p==1)
	  {
	    gain=MCP3425_GAIN_1;
		valid_gain = true;
	  }
      else if(gain_p==2)
	  {
	    gain=MCP3425_GAIN_2;
		valid_gain = true;
	  }
      else if(gain_p==4)
	  {
	    gain=MCP3425_GAIN_4;
		valid_gain = true;
	  }
      else if(gain_p==8)
	  {
	    gain=MCP3425_GAIN_8;
		valid_gain = true;
	  }	  
	  
	  if(valid_gain)
	  {
		if(ch==0)
		{
			for(uint8_t i = 0; i < A2D_4CH_ISO_ADC_NUM_CHANNELS; i++)
			{
			    adc.set_gain(i, gain);
			}
		}
		else
		{
			adc.set_gain(ch-1, gain);
		}
	  }
  }
  
  //INSTR:DR DR,CH
  //DR 15,60,240
  //CH is integer in range of 1-4, or 0. 0 means all 4 channels
  else if (CMDIS(command, "INSTR:DR"))
  {
	  uint8_t data_rate_p = parse_uint8_t(token);
	  uint8_t data_rate = 0;
	  uint8_t ch = parse_channel(token);
      
	  bool valid_data_rate = false;
	  
	  if(data_rate_p==15)
	  {
		  data_rate = MCP3425_DR_15SPS;
		  valid_data_rate = true;
	  }
	  else if(data_rate_p==60)
	  {
		  data_rate = MCP3425_DR_60SPS;
		  valid_data_rate = true;
	  }
	  else if(data_rate_p==240)
	  {
		  data_rate = MCP3425_DR_240SPS;
		  valid_data_rate = true;
	  }
	  
	  if(valid_data_rate)
	  {
	    if(ch==0)
		{
			for(uint8_t i = 0; i < A2D_4CH_ISO_ADC_NUM_CHANNELS; i++)
			{
			    adc.set_data_rate(i, data_rate);
			}
		}
		else
		{
			adc.set_data_rate(ch-1, data_rate);
		}
	  }
  }
  
  //INSTR:MODE MODE,CH
  //MODE 0 = Single Shot, 1 = Continuous
  //CH is integer in range of 1-4, or 0. 0 means all 4 channels
  else if (CMDIS(command, "INSTR:MODE"))
  {
	  uint8_t mode_p = parse_uint8_t(token);
	  uint8_t mode = 0;
	  uint8_t ch = parse_channel(token);
	  
	  bool valid_mode = false;
	  
	  if(mode_p==0)
	  {
		  mode = MCP3425_MODE_SINGLE_SHOT;
		  valid_mode = true;
	  }
	  else if(mode_p==1)
	  {
		  mode = MCP3425_MODE_CONTINUOUS;
		  valid_mode = true;
	  }
	  
	  if(valid_mode)
	  {  
	    if(ch==0)
		{
			for(uint8_t i = 0; i < A2D_4CH_ISO_ADC_NUM_CHANNELS; i++)
			{
			    adc.set_mode(i, mode);
			}
		}
		else
		{
			adc.set_mode(ch-1, mode);
		}
	  }
  }
  
  //INSTR:RS485:SET ADDR
  //Set RS485 address
  else if (CMDIS(command, "INSTR:RS485:SET"))
  {
	  uint8_t addr = parse_uint8_t(token);
	  adc.set_rs485_addr(addr);
  }
  
  //INSTR:RS485?
  //Returns the current RS485 address
  else if (CMDIS(command, "INSTR:RS485?"))
  {
	  Serial.println(adc.get_rs485_addr());
	  Serial.flush();
  }
  
  //INSTR:RS485:SAVE
  //Save current RS485 address to EEPROM
  else if (CMDIS(command, "INSTR:RS485:SAVE"))
  {
	  adc.save_rs485_addr();
  }
  
}

uint8_t parse_uint8_t(char* token)
{
	char delimeters[] = " ,?";
	token = strtok(NULL, delimeters);
	uint8_t val = uint8_t(atoi(token));
	return val;
}

uint8_t parse_channel(char* token)
{
	//returns channel if the channel is not for this device.
	
	//if command is not for this device, and this is the base device, then pass it on over RS485 (and wait for response if applicable)
	//if command is not for this device, and this is not the base device, then ignore the command.
	//if command is for this device, then process the command.
	
	char delimeters[] = " ,?";
	token = strtok(NULL, delimeters);
	uint8_t channel = uint8_t(atoi(token));
	
	if(channel == 0)
	{
		return 0;
	}
	else if(channel_on_this_device(channel))
	{
		channel = channel % A2D_4CH_ISO_ADC_NUM_CHANNELS;
		if(channel == 0)
		{
			channel = A2D_4CH_ISO_ADC_NUM_CHANNELS;
		}
		return channel;
	}
	else
	{
		return channel;
	}
}

bool channel_on_this_device(uint8_t channel)
{
	if((channel >= (adc.get_rs485_addr()) * A2D_4CH_ISO_ADC_NUM_CHANNELS + 1) && 
		(channel <= (adc.get_rs485_addr()) * A2D_4CH_ISO_ADC_NUM_CHANNELS + A2D_4CH_ISO_ADC_NUM_CHANNELS))
	{
		return true;
	}
	return false;
}

void parse_serial(char ser_buf[], char command[], char* token)
{  
	//parses commands from the USB port (base device)

  //we will assume only 1 command is sent at a time
  //so we don't have to deal with SCPI's ';' to send
  //multiple commands on the same line
  
  //split input string on space to extract the command and the parameters
  //strtok replaces the delimeter with NULL to terminate the string
  //strtok maintains a static pointer to the original string passed to it.
  //to get the next token, pass NULL as the first argument.
  
  char delimeters_command[] = " ";
  token = strtok(ser_buf, delimeters_command);
  strcpy(command, token); //copies the token into command.
}

uint8_t parse_serial_2(char ser_buf[], char command[], char* token)
{
	//parses commands from the RS485 port (chained devices)
	
	//returns the rs485 address sent (first part of the serial buffer).
	//if the address is 0, then command will hold what should be printed to USB.
	
	//command format is exactly the same as the USB-based commands, but has the RS485 address at the start.
	//ex: "<ADDR> MEAS:VOLT <CH>?"
	//where <ADDR> is the device at a particular RS485 address
	//and <CH> is the channel in the associated chain.
	//<CH> can be from 0 to 32*A2D_4CH_ISO_ADC_NUM_CHANNELS (0 to 128)
	
	//the base device could receive a command for a specific channel.
	//channels 0 to 4 are base device only (RS485 address 0).
	//channels 5 to 8 correspond to the device with RS485 address 1.
	// A device with RS485 address of X has the channels
	//          (X+1) * A2D_4CH_ISO_ADC_NUM_CHANNELS + 1
	//          to
	//          (X+1) * A2D_4CH_ISO_ADC_NUM_CHANNELS + A2D_4CH_ISO_ADC_NUM_CHANNELS
	
	char delimeters_address_command[] = " ";
	
	//parse address
	token = strtok(ser_buf, delimeters_address_command);
	uint8_t rs485_address = uint8_t(atoi(token));
	
	//parse command
	token = strtok(NULL, delimeters_address_command);
	strcpy(command, token);
	
	return rs485_address;
}

void wait_rs485_response_send_usb(char ser_buf[])
{
	//wait for a response.
	#ifdef DEBUG
	Serial.println(F("Waiting for RS485"));
	#endif
	while(!Serial3.available())
	{
		delayMicroseconds(5);
	}
	#ifdef DEBUG
	Serial.println(F("RS485 Available"));
	#endif
	uint8_t chars_response = Serial3.readBytesUntil(END_CHAR, ser_buf, SER_BUF_LEN);
	#ifdef DEBUG
	Serial.println(F("RS485 Done Reading"));
	#endif
	//send it back over USB.
	Serial.write(ser_buf, chars_response);
	Serial.println("");
	Serial.flush();
}
