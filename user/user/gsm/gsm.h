#ifndef __GSM_H__
#define __GSM_H__

#include "uart/uart.h"
#include "hardware.h"
#include "string.h"
#include "stdbool.h"

void send_at(const char *str);
void send_debug(const char *str);
bool is_timeout(uint32_t start_ms, uint32_t timeout_ms);

void log_raw_line(const char *line);

#endif //__GSM_H__