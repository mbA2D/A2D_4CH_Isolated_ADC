#include "uart_fakes/main.h"
#include <stdio.h>
#include <stdlib.h>
#include "A2D_ScpiLineReader.h"
USART_TypeDef fake_usart;
DMA_Channel_TypeDef fake_channel;
DMA_TypeDef fake_dma;
DWT_Type fake_dwt;
CoreDebug_Type fake_debug;
UART_HandleTypeDef huart3={&fake_usart,{57600}};
uint32_t SystemCoreClock=72000000;
static uint32_t irq_mask,timeout_used;
static unsigned data_reads,read_remaining,inject_after;
static uint32_t uart_pending;
static int tx_result,pin_state;
static bool sr_read,wrap_after_cndtr,tc_after_dma_snapshot;
static uint32_t de_asserted_at,de_released_at,tx_completed_at;
static bool tx_in_progress;
static void feed(unsigned count,uint8_t value,bool interrupts);
#include "../hal/src/uart.c"
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
static void apply_flags(void) {
    if(fake_dma.IFCR&DMA_IFCR_CGIF3)fake_dma.ISR=0;
    else fake_dma.ISR&=~fake_dma.IFCR;
    fake_dma.IFCR=0;
}
uint32_t fake_read(volatile uint32_t *reg) {
    apply_flags();
    if(reg==&fake_channel.CNDTR) {
        if(inject_after&&++read_remaining==inject_after) { inject_after=0;feed(600,'X',false); }
    }
    if(reg==&fake_channel.CNDTR&&wrap_after_cndtr) {
        uint32_t sampled=*reg;wrap_after_cndtr=false;feed(1,'B',false);return sampled;
    }
    if(reg==&fake_dma.ISR&&tc_after_dma_snapshot) {
        uint32_t sampled=*reg;tc_after_dma_snapshot=false;feed(256,'L',false);return sampled;
    }
    if(reg==&fake_dwt.CYCCNT)fake_dwt.CYCCNT+=72;
    if(reg==&fake_usart.SR)sr_read=true;
    if(reg==&fake_usart.DR) {
        ++data_reads;fake_usart.SR&=~USART_SR_RXNE;
        if(sr_read)fake_usart.SR&=~RX_ERRORS;
        sr_read=false;
    }
    return *reg;
}
uint32_t A2D_irq_lock(void){uint32_t old=irq_mask;irq_mask=1;return old;}
void A2D_irq_unlock(uint32_t old){irq_mask=old;}
uint32_t NVIC_GetPendingIRQ(int irq){CHECK(irq==USART3_IRQn);return uart_pending;}
void HAL_NVIC_SetPriority(int irq,int priority,int sub){CHECK(irq==DMA1_Channel3_IRQn&&priority==2&&sub==0);}
void HAL_NVIC_EnableIRQ(int irq){CHECK(irq==DMA1_Channel3_IRQn);}
int HAL_UART_Transmit(UART_HandleTypeDef *u,uint8_t *p,uint16_t n,uint32_t timeout){
    CHECK(u==&huart3&&p&&n<=200&&pin_state==GPIO_PIN_SET);
    timeout_used=timeout;tx_in_progress=true;
    fake_dwt.CYCCNT+=(uint32_t)(((uint64_t)n*10U*SystemCoreClock+u->Init.BaudRate-1U)/u->Init.BaudRate);
    CHECK(pin_state==GPIO_PIN_SET);tx_completed_at=fake_dwt.CYCCNT;tx_in_progress=false;
    return tx_result;
}
void HAL_GPIO_WritePin(int port,int pin,int state){
    CHECK(port==GPIOB&&pin==GPIO_PIN_1);
    if(state==GPIO_PIN_SET)de_asserted_at=fake_dwt.CYCCNT;
    else{CHECK(!tx_in_progress);de_released_at=fake_dwt.CYCCNT;}
    pin_state=state;
}
static void reset(void) {
    memset(&fake_dma,0,sizeof(fake_dma));memset(&fake_channel,0,sizeof(fake_channel));
    memset(&fake_usart,0,sizeof(fake_usart));irq_mask=inject_after=read_remaining=uart_pending=0;huart3.Init.BaudRate=57600;
    sr_read=wrap_after_cndtr=tc_after_dma_snapshot=tx_in_progress=false;
    A2D_uart_start();apply_flags();
}
static void feed(unsigned count,uint8_t value,bool interrupts) {
    for(unsigned i=0;i<count;++i) {
        apply_flags();
        CHECK(fake_channel.CCR&DMA_CCR_EN);
        unsigned index=RX_SIZE-fake_channel.CNDTR;
        ((volatile uint8_t *)fake_channel.CMAR)[index]=value;
        --fake_channel.CNDTR;
        if(fake_channel.CNDTR==RX_SIZE/2)fake_dma.ISR|=DMA_ISR_HTIF3;
        if(!fake_channel.CNDTR){fake_dma.ISR|=DMA_ISR_TCIF3;fake_channel.CNDTR=RX_SIZE;}
        if(interrupts&&fake_dma.ISR){DMA1_Channel3_IRQHandler();apply_flags();}
    }
}
static void text(const char *s){while(*s)feed(1,(uint8_t)*s++,true);}
static void recovered(void) {
    uint8_t out[32];text("tail\nGOOD\n");
    CHECK(A2D_uart_read(out,sizeof(out))==7);
    CHECK(out[0]==0&&out[1]=='\n'&&!memcmp(out+2,"GOOD\n",5));
    CHECK(A2D_uart_available()==0);
}
static size_t reader_limit=64;
static int input_available(void *context){(void)context;return A2D_uart_available();}
static size_t input_read(void *context,char *data,size_t size){
    (void)context;return A2D_uart_read((uint8_t*)data,size<reader_limit?size:reader_limit);
}
static void parser_recovery(bool overflow,size_t limit){
    char storage[201],line[201];A2D_ScpiLineReader reader;
    const A2D_ScpiInput input={NULL,input_available,input_read};
    reset();reader_limit=limit;CHECK(a2d_scpi_reader_init(&reader,storage,sizeof(storage)));
    text("1 7 INSTR:GAIN 8,");CHECK(a2d_scpi_reader_poll(&reader,&input,line)==0);
    if(overflow)feed(600,'X',true);
    else{fake_usart.SR=USART_SR_ORE;USART3_IRQHandler();}
    CHECK(a2d_scpi_reader_poll(&reader,&input,line)==0);
    text("5\n1 8 INSTR:GAIN? 5\n");
    /* The suffix would complete a valid setter if the partial line survived. */
    CHECK(a2d_scpi_reader_poll(&reader,&input,line)==-1);
    CHECK(a2d_scpi_reader_poll(&reader,&input,line)==1&&!strcmp(line,"1 8 INSTR:GAIN? 5"));
    CHECK(a2d_scpi_reader_poll(&reader,&input,line)==0);
}
int main(void) {
    uint8_t out[600];reset();
    CHECK(fake_channel.CNDTR==512&&fake_channel.CPAR==(uintptr_t)&fake_usart.DR);
    CHECK((fake_usart.CR3&(USART_CR3_DMAR|USART_CR3_EIE))==(USART_CR3_DMAR|USART_CR3_EIE));
    text("abc\n");CHECK(A2D_uart_available()==4&&A2D_uart_read(out,2)==2&&!memcmp(out,"ab",2));
    CHECK(A2D_uart_read(out,20)==2&&!memcmp(out,"c\n",2));
    for(unsigned i=0;i<1000;++i){feed(3,(uint8_t)i,true);CHECK(A2D_uart_read(out,3)==3);for(unsigned j=0;j<3;++j)CHECK(out[j]==(uint8_t)i);}
    puts("PASS DMA byte order, partial reads and repeated circular wraps");
    reset();feed(500,'A',true);CHECK(A2D_uart_read(out,500)==500);
    feed(20,'B',false);CHECK(A2D_uart_available()==20&&A2D_uart_read(out,20)==20);
    DMA1_Channel3_IRQHandler();apply_flags();CHECK(A2D_uart_available()==0);
    puts("PASS pending DMA completion counted once before/after its interrupt");
    reset();feed(511,'A',true);CHECK(A2D_uart_read(out,511)==511);
    wrap_after_cndtr=true;CHECK(A2D_uart_available()==1&&!discard);
    CHECK(A2D_uart_read(out,1)==1&&out[0]=='B');DMA1_Channel3_IRQHandler();apply_flags();CHECK(A2D_uart_available()==0);
    reset();feed(256,'H',false);tc_after_dma_snapshot=true;DMA1_Channel3_IRQHandler();apply_flags();
    CHECK((fake_dma.ISR&DMA_ISR_TCIF3)&&wraps==0);
    DMA1_Channel3_IRQHandler();apply_flags();CHECK(wraps==512&&!(fake_dma.ISR&DMA_ISR_TCIF3));
    puts("PASS wrap after CNDTR sample and TC arriving after HT snapshot");
    reset();feed(600,'X',true);CHECK(A2D_uart_available()==0);recovered();
    reset();feed(1600,'X',false);CHECK(A2D_uart_available()==0);DMA1_Channel3_IRQHandler();apply_flags();CHECK(A2D_uart_available()==0);recovered();
    puts("PASS unread overflow and multiple missed DMA wraps discard through LF");
    for(unsigned n=511;n<=513;++n){reset();feed(n,'X',true);CHECK(A2D_uart_available()==(n==511?511:0));}
    reset();feed(511,'X',true);CHECK(A2D_uart_read(out,511)==511);
    fake_usart.SR=USART_SR_ORE;USART3_IRQHandler();CHECK(A2D_uart_available()==0);text("\nGOOD\n");
    CHECK(A2D_uart_read(out,1)==1&&out[0]==0);CHECK(A2D_uart_read(out,1)==1&&out[0]=='\n');
    CHECK(A2D_uart_read(out,5)==5&&!memcmp(out,"GOOD\n",5));
    parser_recovery(false,64);parser_recovery(true,64);parser_recovery(false,1);parser_recovery(true,1);
    puts("PASS 511/512/513 bounds, boundary LF, split marker and real parser partial-setter rejection");
    reset();fake_usart.SR=USART_SR_ORE|USART_SR_RXNE;(void)READ_REG(fake_usart.DR);
    CHECK((fake_usart.SR&USART_SR_ORE)&&!(fake_usart.SR&USART_SR_RXNE));
    (void)READ_REG(fake_usart.SR);(void)READ_REG(fake_usart.DR);CHECK(!(fake_usart.SR&RX_ERRORS));
    for(unsigned bit=1;bit<=8;bit<<=1)for(unsigned present=0;present<2;++present){
        reset();text("BAD");fake_usart.SR=bit|(present?USART_SR_RXNE:0);unsigned before=data_reads;
        USART3_IRQHandler();CHECK(data_reads==before+1&&!(fake_usart.SR&RX_ERRORS));
        CHECK(A2D_uart_available()==0);recovered();
    }
    puts("PASS parity/frame/noise/overrun clearing, including ORE without RXNE");
    reset();text("BAD");uart_pending=1;fake_usart.SR=USART_SR_FE|USART_SR_RXNE;
    (void)READ_REG(fake_usart.SR);
    (void)READ_REG(fake_usart.DR); /* DMA may retire DR before the error ISR runs. */
    CHECK(A2D_uart_read(out,3)==0);
    uart_pending=0;USART3_IRQHandler();CHECK(A2D_uart_available()==0);recovered();
    puts("PASS pending receive error remains invalid after DMA clears status");
    reset();text("BAD");fake_dma.ISR=DMA_ISR_TEIF3;fake_channel.CCR&=~DMA_CCR_EN;DMA1_Channel3_IRQHandler();apply_flags();
    CHECK(fake_channel.CNDTR==512&&A2D_uart_available()==0);recovered();
    reset();text("BAD");read_remaining=0;inject_after=2;CHECK(A2D_uart_read(out,3)==0);
    DMA1_Channel3_IRQHandler();apply_flags();CHECK(A2D_uart_available()==0);recovered();
    puts("PASS DMA transfer-error recovery and DMA overtaking a copy");
    reset();wraps=0xfffffe00U;consumed=wraps;feed(300,'W',true);CHECK(A2D_uart_read(out,300)==300);
    feed(300,'Z',true);CHECK(A2D_uart_read(out,300)==300&&wraps==0);
    puts("PASS unsigned 32-bit producer counter wrap");
    const uint32_t rates[]={9600,19200,57600,115200,230400,250000,460800,1000000};
    for(unsigned i=0;i<sizeof(rates)/sizeof(rates[0]);++i){
        huart3.Init.BaudRate=rates[i];tx_result=HAL_OK;
        uint32_t start=fake_dwt.CYCCNT;A2D_set_rs485_receive(false);
        uint32_t us=(10000000U+rates[i]-1U)/rates[i];if(us<100)us=100;
        CHECK((uint32_t)(de_asserted_at-start)>=72*us&&pin_state==GPIO_PIN_SET);
        CHECK(A2D_uart_write(out,200)==200);
        uint32_t needed=(2000000U+rates[i]-1U)/rates[i]+20U;
        CHECK(timeout_used==(needed>100U?needed:100U));
        A2D_set_rs485_receive(true);CHECK(pin_state==GPIO_PIN_RESET);
        CHECK(de_released_at==tx_completed_at);
    }
    tx_result=1;A2D_set_rs485_receive(false);CHECK(A2D_uart_write(out,200)==0);A2D_set_rs485_receive(true);
    CHECK(A2D_uart_write(out,201)==0&&A2D_uart_write(out,0)==0);
    puts("PASS baud-aware TX bounds, HAL failure and driver turnaround guard");
    return 0;
}
