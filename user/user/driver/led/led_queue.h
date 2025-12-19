#ifndef __LED_QUEUE_H__
#define __LED_QUEUE_H__

#include <stdint.h>
#include <stdbool.h>

#define LED_QUEUE_SIZE 10

typedef enum {
    LED_QUEUE_CMD_NONE = 0,
    LED_QUEUE_CMD_OFF = 1,
    LED_QUEUE_CMD_ON = 2
} led_queue_cmd_t;

void led_queue_init(void);
bool led_queue_push(led_queue_cmd_t cmd);
bool led_queue_pop(led_queue_cmd_t *cmd);
bool led_queue_is_empty(void);
uint8_t led_queue_count(void);

#endif //__LED_QUEUE_H__

