#ifndef __UART_H__
#define __UART_H__

/*
    include 
*/
#include "stm32f10x_usart.h"
#include "third_paty/ringbuff/ringbuff.h"
#include "hardware.h"
/*
    define
*/
#define UART_DEBUG_BUFFER_SIZE 256
#define UART_SIM_BUFFER_SIZE 256

/*
    funcion
*/

//========================================init, send_byte, send_string==========================
void uart_init(void);
void uart_send_byte(USART_TypeDef *uart, uint8_t data);
void uart_send_string(USART_TypeDef *uart,const char *data, uint16_t length);
//========================================uarrt for sim, debug handle==========================
void uart_debug_rx_handler(uint8_t data);
void uart_sim_rx_handler(uint8_t data);
//==============================get data in rx bufff ====================================
uint16_t uart_debug_read(uint8_t *data, uint16_t len);
uint16_t uart_sim_read(uint8_t *data, uint16_t len);
//==============================valib buf have full ====================================
uint16_t uart_debug_available(void);
uint16_t uart_sim_available(void);

#endif // __UART_H__
