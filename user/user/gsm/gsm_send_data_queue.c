#include <string.h>
#include "gsm/gsm_send_data_queue.h"
#include "driver/uart/uart.h"

static gsm_urc_queue_t gsm_urc_queue;

static char gsm_urc_line_buffer[GSM_URC_LINE_MAX_LEN];
static uint16_t gsm_urc_line_index = 0;

void gsm_send_data_queue_init(void) {
    memset(&gsm_urc_queue, 0, sizeof(gsm_urc_queue_t));
    gsm_urc_line_index = 0;
    gsm_urc_queue.push_done = false;
}

static bool gsm_send_data_queue_push(const char *line) {
    if(gsm_urc_queue.count >= GSM_URC_QUEUE_SIZE) {
        return false;
    }
    
    strncpy(gsm_urc_queue.lines[gsm_urc_queue.head], line, GSM_URC_LINE_MAX_LEN - 1);
    gsm_urc_queue.lines[gsm_urc_queue.head][GSM_URC_LINE_MAX_LEN - 1] = '\0';
    
    gsm_urc_queue.head = (gsm_urc_queue.head + 1) % GSM_URC_QUEUE_SIZE;
    gsm_urc_queue.count++;
    gsm_urc_queue.push_done = true;
    return true;
}

bool gsm_send_data_queue_pop(char *line, uint16_t max_len) {
    if(gsm_urc_queue.count == 0) {
        return false;
    }
    
    strncpy(line, gsm_urc_queue.lines[gsm_urc_queue.tail], max_len - 1);
    line[max_len - 1] = '\0';
    
    gsm_urc_queue.tail = (gsm_urc_queue.tail + 1) % GSM_URC_QUEUE_SIZE;
    gsm_urc_queue.count--;

    if (gsm_urc_queue.count == 0) {
        gsm_urc_queue.push_done = false;
    }
    
    return true;
}

void gsm_send_data_queue_proces(void) {
    uint8_t b;

    while (uart_sim_read(&b, 1) > 0) {
        if (b == '\r' || b == '\n') {
            if (gsm_urc_line_index > 0) {
                gsm_urc_line_buffer[gsm_urc_line_index] = '\0';
                gsm_send_data_queue_push(gsm_urc_line_buffer);
                gsm_urc_line_index = 0;
            }
            continue;
        }
        if (gsm_urc_line_index < GSM_URC_LINE_MAX_LEN - 1) {
            gsm_urc_line_buffer[gsm_urc_line_index++] = b;
        } else {
            gsm_urc_line_buffer[GSM_URC_LINE_MAX_LEN - 1] = '\0';
            gsm_send_data_queue_push(gsm_urc_line_buffer);
            gsm_urc_line_index = 0;
        }
    }
}

void delete_line(char *line){
    if (line != NULL) {
        memset(line, 0, strlen(line));
    }
}