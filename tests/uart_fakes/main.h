#ifndef A2D_UART_FAKE_H
#define A2D_UART_FAKE_H
#include "A2D_HAL.h"
#include <string.h>
typedef struct {volatile uint32_t SR,DR,CR1,CR3;} USART_TypeDef;
typedef struct {volatile uint32_t CCR,CNDTR;uintptr_t CPAR,CMAR;} DMA_Channel_TypeDef;
typedef struct {volatile uint32_t ISR,IFCR;} DMA_TypeDef;
typedef struct {volatile uint32_t CTRL,CYCCNT;} DWT_Type;
typedef struct {volatile uint32_t DEMCR;} CoreDebug_Type;
typedef struct {USART_TypeDef *Instance;struct {uint32_t BaudRate;} Init;} UART_HandleTypeDef;
extern USART_TypeDef fake_usart;
extern DMA_Channel_TypeDef fake_channel;
extern DMA_TypeDef fake_dma;
extern DWT_Type fake_dwt;
extern CoreDebug_Type fake_debug;
extern UART_HandleTypeDef huart3;
extern uint32_t SystemCoreClock;
#define USART3 (&fake_usart)
#define DMA1_Channel3 (&fake_channel)
#define DMA1 (&fake_dma)
#define DWT (&fake_dwt)
#define CoreDebug (&fake_debug)
#define CoreDebug_DEMCR_TRCENA_Msk (1U<<24)
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define USART_SR_PE 1U
#define USART_SR_FE 2U
#define USART_SR_NE 4U
#define USART_SR_ORE 8U
#define USART_SR_RXNE 32U
#define USART_CR1_RXNEIE 32U
#define USART_CR1_PEIE 256U
#define USART_CR3_DMAR 64U
#define USART_CR3_EIE 1U
#define DMA_ISR_HTIF3 1024U
#define DMA_ISR_TCIF3 512U
#define DMA_ISR_TEIF3 2048U
#define DMA_IFCR_CGIF3 256U
#define DMA_CCR_MINC 128U
#define DMA_CCR_CIRC 32U
#define DMA_CCR_PL_1 8192U
#define DMA_CCR_HTIE 4U
#define DMA_CCR_TCIE 2U
#define DMA_CCR_TEIE 8U
#define DMA_CCR_EN 1U
#define DMA1_Channel3_IRQn 13
#define USART3_IRQn 39
#define GPIOB 1
#define GPIO_PIN_1 2
#define GPIO_PIN_RESET 0
#define GPIO_PIN_SET 1
#define HAL_OK 0
#define __HAL_RCC_DMA1_CLK_ENABLE() ((void)0)
#define SET_BIT(reg,bits) ((reg)|=(bits))
#define CLEAR_BIT(reg,bits) ((reg)&=~(bits))
uint32_t fake_read(volatile uint32_t *reg);
uint32_t NVIC_GetPendingIRQ(int irq);
#define READ_REG(reg) fake_read(&(reg))
#define __HAL_UART_CLEAR_OREFLAG(uart) do{(void)READ_REG((uart)->Instance->SR);(void)READ_REG((uart)->Instance->DR);}while(0)
void HAL_NVIC_SetPriority(int irq,int priority,int sub);
void HAL_NVIC_EnableIRQ(int irq);
int HAL_UART_Transmit(UART_HandleTypeDef *,uint8_t *,uint16_t,uint32_t);
void HAL_GPIO_WritePin(int,int,int);
#endif
