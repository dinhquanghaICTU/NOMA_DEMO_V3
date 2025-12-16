#ifndef __HARDWARE_H__
#define __HARDWARE_H__  


/*
    include
*/
#include "stdint.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_flash.h"
#include "misc.h"

/*
    define
*/

// define for debug uart ttl pa2-RX(ttl) , pa3-TX(ttl) 
#define DEBUG_TX    GPIO_Pin_2
#define DEBUG_RX    GPIO_Pin_3
#define DEBUG   GPIOA
#define DEBUG_UART USART2

// define for sim uart pa9- Rx(sim), pa10- TX(sim)
#define SIM_TX    GPIO_Pin_9
#define SIM_RX    GPIO_Pin_10 
#define SIM     GPIOA 
#define SIM_UART    USART1 


/*
    funcion
*/
void rcc_config(void);
uint32_t get_tick_ms(void);
void tick_ms_increment(void);
void delay_ms(uint32_t ms);
void systick_config(void);

#endif // __HARDWARE_H__