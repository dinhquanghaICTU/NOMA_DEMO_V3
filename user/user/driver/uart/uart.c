/*
    include
*/
#include "uart.h"

/*
    init value
*/
static uint8_t uart_sim_buffer_data[UART_SIM_BUFFER_SIZE];
static ringbuff_t uart_sim_rx_buff;

static uint8_t uart_debug_rx_buffer[UART_DEBUG_BUFFER_SIZE];
static ringbuff_t uart_debug_rx_buff;

/*
    funcion
*/

// init uart
void uart_init (void){

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2,ENABLE);
    GPIO_InitTypeDef gpio_debug_config;
    
    gpio_debug_config.GPIO_Pin = DEBUG_TX;
    gpio_debug_config.GPIO_Speed = GPIO_Speed_50MHz;
    gpio_debug_config.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(DEBUG, &gpio_debug_config);

    gpio_debug_config.GPIO_Pin =DEBUG_RX;
    gpio_debug_config.GPIO_Mode= GPIO_Mode_IN_FLOATING;
    GPIO_Init(DEBUG, &gpio_debug_config);

    gpio_debug_config.GPIO_Pin = SIM_TX;
    gpio_debug_config.GPIO_Speed = GPIO_Speed_50MHz;
    gpio_debug_config.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(SIM, &gpio_debug_config);

    gpio_debug_config.GPIO_Pin =SIM_RX;
    gpio_debug_config.GPIO_Mode= GPIO_Mode_IN_FLOATING;
    GPIO_Init(SIM, &gpio_debug_config);

    ringbuff_init(&uart_debug_rx_buff, uart_debug_rx_buffer, UART_DEBUG_BUFFER_SIZE);
    ringbuff_init(&uart_sim_rx_buff, uart_sim_buffer_data, UART_SIM_BUFFER_SIZE);

    USART_InitTypeDef uart_debug_config;
    uart_debug_config.USART_BaudRate = 115200;
    uart_debug_config.USART_Parity = USART_Parity_No;
    uart_debug_config.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    uart_debug_config.USART_StopBits = USART_StopBits_1;
    uart_debug_config.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    uart_debug_config.USART_WordLength = USART_WordLength_8b;
    USART_Init(DEBUG_UART, &uart_debug_config);
    
    USART_ITConfig(DEBUG_UART, USART_IT_RXNE, ENABLE);
    NVIC_SetPriority(USART2_IRQn, 1);  
    NVIC_EnableIRQ(USART2_IRQn);
    
    USART_Cmd(DEBUG_UART, ENABLE);

    USART_InitTypeDef uart_sim_config;
    uart_sim_config.USART_BaudRate = 115200;
    uart_sim_config.USART_Parity = USART_Parity_No;
    uart_sim_config.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    uart_sim_config.USART_StopBits = USART_StopBits_1;
    uart_sim_config.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    uart_sim_config.USART_WordLength = USART_WordLength_8b;
    USART_Init(SIM_UART, &uart_sim_config);
    
    USART_ITConfig(SIM_UART, USART_IT_RXNE, ENABLE);
    NVIC_SetPriority(USART1_IRQn, 2);  
    NVIC_EnableIRQ(USART1_IRQn);
    
    USART_Cmd(SIM_UART, ENABLE);
}

// write data to debug rx buff
void uart_debug_rx_handler(uint8_t data) {
    ringbuff_write(&uart_debug_rx_buff, &data, 1);
}

// write data to sim rx buff
void uart_sim_rx_handler(uint8_t data) {
    ringbuff_write(&uart_sim_rx_buff, &data, 1);
}

//get data in sim rx buff
uint16_t uart_sim_read(uint8_t *data, uint16_t len) {
    return (uint16_t)ringbuff_read(&uart_sim_rx_buff, data, len);
}

//get data in debug rx buff
uint16_t uart_debug_read(uint8_t *data, uint16_t len) {
    return (uint16_t)ringbuff_read(&uart_debug_rx_buff, data, len);
}

// check bufff is full
uint16_t uart_debug_available(void) {
    return (uint16_t)ringbuff_get_full(&uart_debug_rx_buff);
}

uint16_t uart_sim_available(void) {
    return (uint16_t)ringbuff_get_full(&uart_sim_rx_buff);
}
// send byte and string 
void uart_send_byte(USART_TypeDef *uart, uint8_t data) {
    while(USART_GetFlagStatus(uart, USART_FLAG_TXE) == RESET);
    USART_SendData(uart, data);
}

void uart_send_string(USART_TypeDef *uart,const char *data, uint16_t length) {
    uint16_t i;
    for(i = 0; i < length; i++) {
        uart_send_byte(uart,data[i]);
    }
}