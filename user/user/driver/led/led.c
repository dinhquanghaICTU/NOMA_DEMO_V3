#include "led.h"
#include "../w25Qx/w25Qx.h"
#include <string.h>

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

// Áp dụng lệnh chuỗi "0"/"1" cho LED và lưu state xuống flash
led_cmd_result_t led_apply_command(const char *cmd) {
    if (!cmd) return LED_CMD_INVALID;

    // Bỏ khoảng trắng đầu/cuối
    const char *p = cmd;
    while (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t' || *p == '\"') p++;
    const char *q = cmd + strlen(cmd);
    while (q > p && (q[-1] == ' ' || q[-1] == '\r' || q[-1] == '\n' || q[-1] == '\t' || q[-1] == '\"')) q--;

    size_t len = (q > p) ? (size_t)(q - p) : 0;
    if (len != 1) return LED_CMD_INVALID;

    char c = p[0];
    if (c == '0') {
        led_set_state(0);
        led_state_save(0);
        return LED_CMD_OFF;
    } else if (c == '1') {
        led_set_state(1);
        led_state_save(1);
        return LED_CMD_ON;
    }
    return LED_CMD_INVALID;
}