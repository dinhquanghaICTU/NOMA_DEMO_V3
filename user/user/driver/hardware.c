#include "hardware.h"

/*
    init value
*/
static uint32_t tick_ms = 0;

/*
    funcion handle 
*/

// ========================================Systik,set delay,config RCC====================
uint32_t get_tick_ms(void) {
    return tick_ms;
}

void tick_ms_increment(void) {
    tick_ms++;
}

void delay_ms(uint32_t ms) {
    uint32_t start_time = get_tick_ms();
    while((get_tick_ms() - start_time) < ms) {}
}

void rcc_config(void){
    RCC_HSEConfig(RCC_HSE_ON);
    ErrorStatus HSEStatus = RCC_WaitForHSEStartUp();
    if(HSEStatus == SUCCESS) {
        FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);  
        FLASH_SetLatency(FLASH_Latency_2); 
        RCC_HCLKConfig(RCC_SYSCLK_Div1);
        RCC_PCLK2Config(RCC_HCLK_Div1);
        RCC_PCLK1Config(RCC_HCLK_Div2);
        RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_9);
        RCC_PLLCmd(ENABLE);
        
        while(RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET) {}
        RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
        
        while(RCC_GetSYSCLKSource() != 0x08) {}
    }
    
    SystemCoreClockUpdate();
}

void systick_config(void){
    
    SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK);
    
    if(SysTick_Config(SystemCoreClock / 1000) != 0) {
        
        while(1) {
            
        }
    }
    NVIC_SetPriority(SysTick_IRQn, (1 << __NVIC_PRIO_BITS) - 1);
}