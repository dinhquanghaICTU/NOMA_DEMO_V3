#include "led_queue.h"
#include <string.h>

typedef struct {
    led_queue_cmd_t cmds[LED_QUEUE_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} led_queue_t;

static led_queue_t led_queue;

void led_queue_init(void) {
    memset(&led_queue, 0, sizeof(led_queue_t));
}

bool led_queue_push(led_queue_cmd_t cmd) {
    if (led_queue.count >= LED_QUEUE_SIZE) {
        return false; // Queue full
    }
    
    led_queue.cmds[led_queue.head] = cmd;
    led_queue.head = (led_queue.head + 1) % LED_QUEUE_SIZE;
    led_queue.count++;
    
    return true;
}

bool led_queue_pop(led_queue_cmd_t *cmd) {
    if (led_queue.count == 0 || cmd == NULL) {
        return false; // Queue empty
    }
    
    *cmd = led_queue.cmds[led_queue.tail];
    led_queue.tail = (led_queue.tail + 1) % LED_QUEUE_SIZE;
    led_queue.count--;
    
    return true;
}

bool led_queue_is_empty(void) {
    return (led_queue.count == 0);
}

uint8_t led_queue_count(void) {
    return led_queue.count;
}

