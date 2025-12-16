#include "led.h"
#include "../w25Qx/w25Qx.h"

static uint8_t current_led_state = 0;

void led_init(void){
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC,ENABLE);
    GPIO_InitTypeDef gpio_config_led;

    gpio_config_led.GPIO_Pin = GPIO_Pin_13;
    gpio_config_led.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio_config_led.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &gpio_config_led);
    
    turn_off_led();
}

void turn_on_led(void){
    GPIO_SetBits(GPIOC, GPIO_Pin_13);
}

void turn_off_led(void){
    GPIO_ResetBits(GPIOC, GPIO_Pin_13); 
}

void led_state_init(void) {
    current_led_state = led_state_load();
    led_set_state(current_led_state);
}

void led_state_save(uint8_t state) {
    if (state > 1) return;
    
    w25qxx_erase_sector(W25Q_LED_STATE_ADDR);
    
    uint8_t data[4] = {state, 0xFF, 0xFF, 0xFF};

    w25qxx_write_page(W25Q_LED_STATE_ADDR, data, 4);
    
    current_led_state = state;
}

uint8_t led_state_load(void) {
    uint8_t data[4];
    w25qxx_read(W25Q_LED_STATE_ADDR, data, 4);
    
    uint8_t state = data[0];
    if (state <= 1) {
        return state;
    }
    return 0;
}

void led_set_state(uint8_t state) {
    if (state == 1) {
        turn_on_led();
    } else {
        turn_off_led();
    }
    current_led_state = state;
}