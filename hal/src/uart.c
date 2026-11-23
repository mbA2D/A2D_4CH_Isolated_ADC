#include "main.h"

enum { RX_SIZE = 512, RX_MASK = RX_SIZE - 1 };
static volatile uint8_t rx[RX_SIZE];
static volatile uint32_t wraps;
static volatile bool fault;
static uint32_t consumed;
static bool discard;
static unsigned marker;
#define RX_ERRORS (USART_SR_PE | USART_SR_FE | USART_SR_NE | USART_SR_ORE)
#define DMA_EVENTS (DMA_ISR_HTIF3 | DMA_ISR_TCIF3 | DMA_ISR_TEIF3)

/* Called with interrupts masked. DMA itself continues receiving. */
static uint32_t produced(void) {
    uint32_t base = wraps;
    uint32_t remaining = READ_REG(DMA1_Channel3->CNDTR);
    if (READ_REG(DMA1->ISR) & DMA_ISR_TCIF3) {
        base += RX_SIZE;
        remaining = READ_REG(DMA1_Channel3->CNDTR);
    }
    return base + RX_SIZE - remaining;
}
static bool failed(void) {
    const uint32_t pending = READ_REG(DMA1->ISR);
    return fault || NVIC_GetPendingIRQ(USART3_IRQn) || (READ_REG(USART3->SR) & RX_ERRORS) || (pending & DMA_ISR_TEIF3) ||
        (pending & (DMA_ISR_HTIF3 | DMA_ISR_TCIF3)) == (DMA_ISR_HTIF3 | DMA_ISR_TCIF3);
}
static void invalidate(uint32_t end) {
    consumed = end; fault = false; discard = true; marker = 0;
}
static uint32_t synchronize(void) {
    const uint32_t end = produced();
    if (failed() || (uint32_t)(end - consumed) >= RX_SIZE) invalidate(end);
    return end;
}
void A2D_uart_start(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __HAL_RCC_DMA1_CLK_ENABLE();
    DMA1_Channel3->CCR = 0;
    DMA1->IFCR = DMA_IFCR_CGIF3;
    DMA1_Channel3->CPAR = (uintptr_t)&USART3->DR;
    DMA1_Channel3->CMAR = (uintptr_t)rx;
    DMA1_Channel3->CNDTR = RX_SIZE;
    wraps = consumed = 0; fault = discard = false; marker = 0;
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    CLEAR_BIT(USART3->CR1, USART_CR1_RXNEIE | USART_CR1_PEIE);
    DMA1_Channel3->CCR = DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_PL_1 |
        DMA_CCR_HTIE | DMA_CCR_TCIE | DMA_CCR_TEIE | DMA_CCR_EN;
    SET_BIT(USART3->CR3, USART_CR3_DMAR | USART_CR3_EIE);
    HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
}
void DMA1_Channel3_IRQHandler(void) {
    const uint32_t events = READ_REG(DMA1->ISR) & DMA_EVENTS;
    /* Clearing only observed flags preserves an event arriving during this ISR. */
    DMA1->IFCR = events;
    if (events & DMA_ISR_TCIF3) wraps += RX_SIZE;
    if ((events & (DMA_ISR_HTIF3 | DMA_ISR_TCIF3)) == (DMA_ISR_HTIF3 | DMA_ISR_TCIF3)) fault = true;
    if (events & DMA_ISR_TEIF3) {
        CLEAR_BIT(DMA1_Channel3->CCR, DMA_CCR_EN);
        DMA1_Channel3->CNDTR = RX_SIZE;
        wraps = 0; fault = true;
        SET_BIT(DMA1_Channel3->CCR, DMA_CCR_EN);
    }
}
void USART3_IRQHandler(void) {
    const uint32_t status = READ_REG(USART3->SR);
    if (status & RX_ERRORS) {
        /* SR then DR clears errors even when ORE is set without RXNE. */
        (void)READ_REG(USART3->DR);
    }
    /* Only receive-error interrupts are enabled; DMA may already have cleared SR. */
    fault = true;
}
int A2D_uart_available(void) {
    const uint32_t mask = A2D_irq_lock();
    const uint32_t end = synchronize();
    const int count = (int)(end - consumed) + (int)marker;
    A2D_irq_unlock(mask); return count;
}
size_t A2D_uart_read(uint8_t *data, size_t size) {
    const uint32_t mask = A2D_irq_lock();
    const uint32_t end = synchronize(), first = consumed;
    size_t count = 0;
    while (count < size && (marker || consumed != end)) {
        if (marker) {
            data[count++] = marker == 2 ? 0 : '\n'; --marker;
        } else {
            const uint8_t value = rx[consumed++ & RX_MASK];
            if (discard) { if (value == '\n') { discard = false; marker = 2; } }
            else data[count++] = value;
        }
    }
    /* Never hand the parser a copy that DMA overtook while we were reading. */
    const uint32_t after = produced();
    if (failed() || (uint32_t)(after - first) >= RX_SIZE) { invalidate(after); count = 0; }
    A2D_irq_unlock(mask); return count;
}
size_t A2D_uart_write(const uint8_t *data, size_t size) {
    if (!size || size > 200 || !huart3.Init.BaudRate) return 0;
    const uint32_t wire_ms = ((uint32_t)size * 10000U + huart3.Init.BaudRate - 1U) / huart3.Init.BaudRate;
    const uint32_t timeout = wire_ms + 20U > 100U ? wire_ms + 20U : 100U;
    /* HAL waits for the final stop bit (TC), before DE is released by the caller. */
    return HAL_UART_Transmit(&huart3, (uint8_t *)data, (uint16_t)size, timeout) == HAL_OK ? size : 0;
}
void A2D_set_rs485_receive(bool receive) {
    if (!receive) {
        /* Allow the other board to finish its stop bit and release its driver. */
        uint32_t us = (10000000U + huart3.Init.BaudRate - 1U) / huart3.Init.BaudRate;
        if (us < 100U) us = 100U;
        const uint32_t start = READ_REG(DWT->CYCCNT), cycles = (SystemCoreClock / 1000000U) * us;
        while ((uint32_t)(READ_REG(DWT->CYCCNT) - start) < cycles) {}
    }
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, receive ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
