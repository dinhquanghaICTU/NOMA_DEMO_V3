#include "gsm_sms.h"
#include "gsm/gsm.h"
#include "gsm/gsm_send_data_queue.h"
#include "../urc/urc.h"
#include "driver/W25Qx/w25qx.h"

#include <string.h>
#include <stdio.h>

#define TIME_OUT           10000
#define SMS_TARGET_ADDR    0x000000U          
#define SMS_TARGET_SIZE    16                 

gsm_sms_ctx_t gsm_sms_ctx;
static urc_t  sms_urc;


static bool gsm_sms_validate_phone(const char *phone) {
    if (!phone || phone[0] == '\0') {
        return false;
    }
    
    size_t len = strlen(phone);
    if (len < 8 || len > 15) {  
        return false;
    }
    
    
    const char *p = phone;
    if (*p == '+') {
        p++;
        len--;
        if (len < 8 || len > 15) {
            return false;
        }
    }
    
    
    for (; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    
    return true;
}

void gsm_sms_init(void){
    memset(&gsm_sms_ctx, 0, sizeof(gsm_sms_ctx));
    gsm_sms_ctx.state       = GSM_SMS_IDLE;
    gsm_sms_ctx.timeout_ms  = TIME_OUT;
    gsm_sms_ctx.retry_count = 0;

    
    uint8_t buf[SMS_TARGET_SIZE];
    w25qxx_read(SMS_TARGET_ADDR, buf, SMS_TARGET_SIZE);

    bool all_ff_or_zero = true;
    for (uint8_t i = 0; i < SMS_TARGET_SIZE; i++) {
        if (buf[i] != 0xFF && buf[i] != 0x00) {
            all_ff_or_zero = false;
            break;
        }
    }

    if (!all_ff_or_zero) {
        
        memcpy(gsm_sms_ctx.target_phone, buf, SMS_TARGET_SIZE - 1);
        gsm_sms_ctx.target_phone[SMS_TARGET_SIZE - 1] = '\0';
        
        
        if (gsm_sms_validate_phone(gsm_sms_ctx.target_phone)) {
            gsm_sms_ctx.target_valid = true;
            send_debug(">>> [SMS] load target from flash: ");
            send_debug(gsm_sms_ctx.target_phone);
            send_debug("\r\n");
        } else {
            gsm_sms_ctx.target_valid = false;
            send_debug(">>> [SMS] số không hợp lệ trong flash: ");
            send_debug(gsm_sms_ctx.target_phone);
            send_debug("\r\n");
        }
    } else {
        gsm_sms_ctx.target_valid = false;
        send_debug(">>> [SMS] no target in flash\r\n");
    }
}

static void gsm_sms_reset_flow(void){
    gsm_sms_ctx.send.step    = 0;
    gsm_sms_ctx.send.retry   = 0;
    gsm_sms_ctx.receive.step = 0;
}

static void gsm_sms_handle_error(void)
{
    gsm_sms_ctx.retry_count++;
    if (gsm_sms_ctx.retry_count < 3) {
        send_debug(">>> [SMS] Retry ");
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", gsm_sms_ctx.retry_count);
        send_debug(buf);
        send_debug("/3\r\n");

        gsm_sms_ctx.state      = GSM_SMS_IDLE;
        gsm_sms_reset_flow();
        gsm_sms_ctx.time_stamp = get_tick_ms();
    } else {
        send_debug(">>> [SMS] Retry 3 times failed\r\n");
        gsm_sms_ctx.state       = GSM_SMS_ERROR;
        gsm_sms_reset_flow();
        gsm_sms_ctx.retry_count = 0;
    }
}


bool gsm_sms_send(const char *phone, const char *text){
    if (gsm_sms_ctx.state != GSM_SMS_IDLE) {
        send_debug(">>> [SMS] busy\r\n");
        return false;
    }

    if (!gsm_sms_ctx.target_valid) {
        send_debug(">>> [SMS] no target phone\r\n");
        return false;
    }

    
    if (phone && phone[0] != '\0' &&
        strncmp(phone, gsm_sms_ctx.target_phone, sizeof(gsm_sms_ctx.target_phone)) != 0) {
        send_debug(">>> [SMS] invalid phone (not target)\r\n");
        return false;
    }

    if (!text || text[0] == '\0') {
        send_debug(">>> [SMS] empty text\r\n");
        return false;
    }

    
    strncpy(gsm_sms_ctx.send.phone, gsm_sms_ctx.target_phone,
            sizeof(gsm_sms_ctx.send.phone) - 1);
    gsm_sms_ctx.send.phone[sizeof(gsm_sms_ctx.send.phone) - 1] = '\0';

    strncpy(gsm_sms_ctx.send.text, text,
            sizeof(gsm_sms_ctx.send.text) - 1);
    gsm_sms_ctx.send.text[sizeof(gsm_sms_ctx.send.text) - 1] = '\0';

    gsm_sms_ctx.send.step  = 0;
    gsm_sms_ctx.send.retry = 0;
    gsm_sms_ctx.state      = GSM_SMS_SEND;

    return true;
}

bool gsm_sms_phase_send(void){
    char line[80];
    switch (gsm_sms_ctx.send.step) {
        case 0: {
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"\r\n", gsm_sms_ctx.send.phone);
            send_at(cmd);
            gsm_sms_ctx.send.step    = 1;
            gsm_sms_ctx.time_stamp   = get_tick_ms();
            break;
        }

        case 1: { 
                // Sau ~200 ms kể từ AT+CMGS, gửi nội dung + Ctrl+Z để tránh mất prompt '>'
                if (is_timeout(gsm_sms_ctx.time_stamp, 200)){
                    uart_send_string(SIM_UART,(uint8_t*)gsm_sms_ctx.send.text,(uint16_t)strlen(gsm_sms_ctx.send.text));
                    delay_ms(20); // cho modem kịp nhận text
                    uart_send_byte(SIM_UART,0x1A); 
                    gsm_sms_ctx.send.step  = 2;
                    gsm_sms_ctx.time_stamp = get_tick_ms();
                    return true;
                }

                // Nếu modem trả lỗi sớm
                if (gsm_send_data_queue_pop(line, sizeof(line))) {
                    log_raw_line(line);
                    if (at_parser_line(line, &sms_urc)) {
                        if (sms_urc.type == URC_CMS_ERROR ||
                            sms_urc.type == URC_ERROR) {
                            gsm_sms_handle_error();
                            return false;
                        }
                    }
                }

                if (is_timeout(gsm_sms_ctx.time_stamp, gsm_sms_ctx.timeout_ms)) {
                    send_debug(">>> [SMS] timeout waiting '>'\r\n");
                    gsm_sms_handle_error();
                    return false;
                }
                break;
            }
        case 2: { 
            if (gsm_send_data_queue_pop(line, sizeof(line))) {
                log_raw_line(line);
                if (at_parser_line(line, &sms_urc)) {
                    if (sms_urc.type == URC_CMGS) {
                        
                    } else if (sms_urc.type == URC_OK) {
                        send_debug(">>> [SMS] send OK\r\n");
                        gsm_sms_ctx.state       = GSM_SMS_DONE;
                        gsm_sms_ctx.retry_count = 0;
                        gsm_sms_reset_flow();
                    } else if (sms_urc.type == URC_CMS_ERROR ||
                               sms_urc.type == URC_ERROR) {    
                        gsm_sms_handle_error();
                        return false;
                    }
                }
            }

            if (is_timeout(gsm_sms_ctx.time_stamp, gsm_sms_ctx.timeout_ms)) {
                send_debug(">>> [SMS] timeout waiting OK\r\n");
                gsm_sms_handle_error();
                return false;
            }
            break;
        }
        default:
            break;
        }
    return false;
}


bool gsm_sms_reciv (void){
    char line [200];
    switch (gsm_sms_ctx.receive.step)
    {
    case 0:
        send_debug(">>> turn on nontify\r\n");
        send_at("AT+CNMI=2,1,0,0,0\r\n");
        gsm_sms_ctx.time_stamp = get_tick_ms();
        gsm_sms_ctx.receive.step = 1;
        break;
    
    case 1:
        if(gsm_send_data_queue_pop(line, sizeof(line))){
            if(at_parser_line(line, &sms_urc)){
                if(sms_urc.type == URC_CMTI){
                    gsm_sms_ctx.receive.index = sms_urc.v1;
                    gsm_sms_ctx.receive.step  = 2;
                    gsm_sms_ctx.time_stamp    = get_tick_ms();
                }
            }
        }
        break;
    case 2:{ 
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "AT+CMGR=%d\r\n", gsm_sms_ctx.receive.index);
        send_at(cmd);
        gsm_sms_ctx.receive.step  = 3;
        gsm_sms_ctx.time_stamp    = get_tick_ms();
        break;
    }
    case 3:
        if (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
            if (at_parser_line(line, &sms_urc)) {
                
                if (sms_urc.type == URC_ERROR || sms_urc.type == URC_CMS_ERROR) {
                    gsm_sms_handle_error();
                    return false;
                }
            } else {

                strncpy(gsm_sms_ctx.receive.text, line,
                        sizeof(gsm_sms_ctx.receive.text) - 1);
                gsm_sms_ctx.receive.text[sizeof(gsm_sms_ctx.receive.text) - 1] = '\0';

                gsm_sms_ctx.receive.step = 4;
                gsm_sms_ctx.time_stamp   = get_tick_ms();
                return true;
            }
        }
        if (is_timeout(gsm_sms_ctx.time_stamp, TIME_OUT)) {
        	send_debug("time out get message\r\n");
            gsm_sms_handle_error();
            return false;
        }
        
        break;
    case 4: { 
        char cmd[24];
        snprintf(cmd, sizeof(cmd), "AT+CMGD=%d\r\n", gsm_sms_ctx.receive.index);
        send_at(cmd);
        gsm_sms_ctx.receive.step  = 5;
        gsm_sms_ctx.time_stamp    = get_tick_ms();
        break;
    }

    case 5: 
        if (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
            if (at_parser_line(line, &sms_urc)) {
                if (sms_urc.type == URC_OK) {
                    send_debug(">>> [SMS] delete done\r\n");
                    gsm_sms_ctx.state = GSM_SMS_IDLE; 
                    gsm_sms_ctx.receive.step = 0;
                    return true;
                } else if (sms_urc.type == URC_ERROR || sms_urc.type == URC_CMS_ERROR) {
                    gsm_sms_ctx.receive.step = 0;
                    gsm_sms_ctx.state = GSM_SMS_IDLE;
                    return false;
                }
            }
        }
        if (is_timeout(gsm_sms_ctx.time_stamp, TIME_OUT)) {
        	send_debug("time out delete message\r\n");
            gsm_sms_ctx.receive.step = 0;
            gsm_sms_ctx.state = GSM_SMS_IDLE;
            return false;
        }
        
        break;
    default:
        break;
    }
    return false;
}

void gsm_sms_process(void){
    switch (gsm_sms_ctx.state) {
    case GSM_SMS_IDLE:

        break;
    case GSM_SMS_SEND:
        gsm_sms_phase_send();
        break;
    
    case GSM_SMS_RECEIVE:
        gsm_sms_reciv();
        break;
    case GSM_SMS_DONE:
        gsm_sms_ctx.receive.step = 0;
        gsm_sms_ctx.state = GSM_SMS_IDLE;
        break;
    default:
        break;
    }
}


bool gsm_sms_has_new(void){
    return false;
}

bool gsm_sms_set_target(const char *phone){
    if (!phone) return false;
    
    
    if (!gsm_sms_validate_phone(phone)) {
        send_debug(">>> [SMS] số không hợp lệ: ");
        send_debug(phone);
        send_debug("\r\n");
        return false;
    }
    
    size_t len = strlen(phone);
    if (len >= sizeof(gsm_sms_ctx.target_phone)) {
        return false;
    }

    
    
    uint32_t sector_base = SMS_TARGET_ADDR & ~(W25QXX_SECTOR_SIZE - 1U);
    w25qxx_erase_sector(sector_base);

    
    uint8_t buf[SMS_TARGET_SIZE];
    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, phone, len);
    buf[(len < SMS_TARGET_SIZE - 1) ? len : (SMS_TARGET_SIZE - 1)] = '\0';

    w25qxx_write_page(SMS_TARGET_ADDR, buf, SMS_TARGET_SIZE);

    
    strncpy(gsm_sms_ctx.target_phone, (const char*)buf,
            sizeof(gsm_sms_ctx.target_phone) - 1);
    gsm_sms_ctx.target_phone[sizeof(gsm_sms_ctx.target_phone) - 1] = '\0';
    gsm_sms_ctx.target_valid = true;

    send_debug(">>> [SMS] save target to flash: ");
    send_debug(gsm_sms_ctx.target_phone);
    send_debug("\r\n");

    return true;
}

const char* gsm_sms_get_target(void){
    if (!gsm_sms_ctx.target_valid) return NULL;
    return gsm_sms_ctx.target_phone;
}


