#include "main.h"
#include "usbd_core.h"
extern USBD_HandleTypeDef hUsbDevice;
I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart3;

void Error_Handler(void) { __disable_irq();for(;;) {} }
uint32_t A2D_millis(void) { return HAL_GetTick(); }
void A2D_delay(uint32_t ms) { HAL_Delay(ms); }
uint32_t A2D_irq_lock(void) { uint32_t mask=__get_PRIMASK();__disable_irq();return mask; }
void A2D_irq_unlock(uint32_t mask) { __set_PRIMASK(mask); }
void A2D_set_led(bool on) { HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,on?GPIO_PIN_RESET:GPIO_PIN_SET); }
bool A2D_i2c_write(uint8_t address,uint8_t value) {
    return HAL_I2C_Master_Transmit(&hi2c1,(uint16_t)address<<1,&value,1,10)==HAL_OK;
}
bool A2D_i2c_read(uint8_t address,uint8_t *data,size_t size) {
    return size<=3 && HAL_I2C_Master_Receive(&hi2c1,(uint16_t)address<<1,data,(uint16_t)size,10)==HAL_OK;
}
int A2D_port_available(unsigned port) { return port==A2D_PORT_USB?A2D_usb_available():A2D_uart_available(); }
size_t A2D_port_read(unsigned port,uint8_t *data,size_t size) { return port==A2D_PORT_USB?A2D_usb_read(data,size):A2D_uart_read(data,size); }
size_t A2D_port_write(unsigned port,const uint8_t *data,size_t size) { return port==A2D_PORT_USB?A2D_usb_write(data,size):A2D_uart_write(data,size); }
int A2D_port_write_space(unsigned port) { return port==A2D_PORT_USB?A2D_usb_write_space():200; }
bool A2D_port_connected(unsigned port) { return port!=A2D_PORT_USB || A2D_usb_session_open(); }
void A2D_enter_bootloader(void) {
    USBD_Stop(&hUsbDevice);USBD_DeInit(&hUsbDevice);
    HAL_GPIO_WritePin(GPIOA,GPIO_PIN_12,GPIO_PIN_RESET);
    GPIO_InitTypeDef pin={0};pin.Pin=GPIO_PIN_12;pin.Mode=GPIO_MODE_OUTPUT_OD;pin.Speed=GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA,&pin);HAL_Delay(500);NVIC_SystemReset();
}
void SysTick_Handler(void) { HAL_IncTick(); }
void USB_LP_CAN1_RX0_IRQHandler(void) { HAL_PCD_IRQHandler(&hpcd); }
static void clocks(void) {
    RCC_OscInitTypeDef osc={0};RCC_ClkInitTypeDef clk={0};RCC_PeriphCLKInitTypeDef usb={0};
    osc.OscillatorType=RCC_OSCILLATORTYPE_HSE;osc.HSEState=RCC_HSE_ON;
    osc.PLL.PLLState=RCC_PLL_ON;osc.PLL.PLLSource=RCC_PLLSOURCE_HSE;osc.PLL.PLLMUL=RCC_PLL_MUL9;
    if(HAL_RCC_OscConfig(&osc)!=HAL_OK)Error_Handler();
    clk.ClockType=RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource=RCC_SYSCLKSOURCE_PLLCLK;clk.AHBCLKDivider=RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider=RCC_HCLK_DIV2;clk.APB2CLKDivider=RCC_HCLK_DIV1;
    if(HAL_RCC_ClockConfig(&clk,FLASH_LATENCY_2)!=HAL_OK)Error_Handler();
    usb.PeriphClockSelection=RCC_PERIPHCLK_USB;usb.UsbClockSelection=RCC_USBCLKSOURCE_PLL_DIV1_5;
    if(HAL_RCCEx_PeriphCLKConfig(&usb)!=HAL_OK)Error_Handler();
}
static void peripherals(void) {
    __HAL_RCC_AFIO_CLK_ENABLE();__HAL_AFIO_REMAP_SWJ_NOJTAG();
    __HAL_RCC_GPIOA_CLK_ENABLE();__HAL_RCC_GPIOB_CLK_ENABLE();__HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef pin={0};pin.Mode=GPIO_MODE_OUTPUT_PP;pin.Speed=GPIO_SPEED_FREQ_LOW;
    pin.Pin=GPIO_PIN_13;HAL_GPIO_Init(GPIOC,&pin);A2D_set_led(false);
    pin.Pin=GPIO_PIN_1;HAL_GPIO_Init(GPIOB,&pin);A2D_set_rs485_receive(true);
    __HAL_RCC_I2C1_CLK_ENABLE();pin.Pin=GPIO_PIN_6|GPIO_PIN_7;pin.Mode=GPIO_MODE_AF_OD;pin.Speed=GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB,&pin);
    hi2c1.Instance=I2C1;hi2c1.Init.ClockSpeed=100000;hi2c1.Init.DutyCycle=I2C_DUTYCYCLE_2;
    hi2c1.Init.AddressingMode=I2C_ADDRESSINGMODE_7BIT;hi2c1.Init.DualAddressMode=I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode=I2C_GENERALCALL_DISABLE;hi2c1.Init.NoStretchMode=I2C_NOSTRETCH_DISABLE;
    if(HAL_I2C_Init(&hi2c1)!=HAL_OK)Error_Handler();
    __HAL_RCC_USART3_CLK_ENABLE();pin.Pin=GPIO_PIN_10;pin.Mode=GPIO_MODE_AF_PP;HAL_GPIO_Init(GPIOB,&pin);
    pin.Pin=GPIO_PIN_11;pin.Mode=GPIO_MODE_INPUT;pin.Pull=GPIO_PULLUP;HAL_GPIO_Init(GPIOB,&pin);
    huart3.Instance=USART3;huart3.Init.BaudRate=230400;huart3.Init.WordLength=UART_WORDLENGTH_8B;
    huart3.Init.StopBits=UART_STOPBITS_1;huart3.Init.Parity=UART_PARITY_NONE;
    huart3.Init.Mode=UART_MODE_TX_RX;huart3.Init.HwFlowCtl=UART_HWCONTROL_NONE;huart3.Init.OverSampling=UART_OVERSAMPLING_16;
    if(HAL_UART_Init(&huart3)!=HAL_OK)Error_Handler();
    HAL_NVIC_SetPriority(USART3_IRQn,2,0);HAL_NVIC_EnableIRQ(USART3_IRQn);
    A2D_uart_start();
}
int main(void) {
    HAL_Init();clocks();peripherals();A2D_usb_init();A2D_app_init();
    for(;;)A2D_app_poll();
}
