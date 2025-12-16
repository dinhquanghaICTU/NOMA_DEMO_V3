#ifndef __LED_H__
#define __LED_H__

#include "hardware.h"
#include <stdint.h>

#define W25Q_LED_STATE_ADDR  0x001000U

void led_init(void);
void turn_on_led(void);
void turn_off_led(void);
void led_state_init(void);
void led_state_save(uint8_t state);
uint8_t led_state_load(void);
void led_set_state(uint8_t state);

#endif 