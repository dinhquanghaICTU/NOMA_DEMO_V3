#include "gsm.h"

void send_at(const char *str){
    uart_send_string(SIM_UART, str, strlen(str));
}

void send_debug(const char *str){
    uart_send_string(DEBUG_UART, str, strlen(str));
}

bool is_timeout(uint32_t start_ms, uint32_t timeout_ms) {
    return (get_tick_ms() - start_ms) >= timeout_ms;
}

void log_raw_line(const char *line){
    send_debug(line);
    send_debug("\r\n"); 
}