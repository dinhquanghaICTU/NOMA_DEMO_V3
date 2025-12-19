#include "gsm_sms.h"
#include "gsm/gsm.h"
#include "gsm/gsm_send_data_queue.h"
#include "../urc/urc.h"
#include "driver/W25Qx/w25qx.h"
#include "driver/led/led.h"
#include "driver/led/led_queue.h"
#include "third_paty/jsmn/jsmn.h"

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

    // Xóa sạch toàn bộ SMS trong SIM khi khởi động (tránh đầy bộ nhớ)
    send_debug(">>> [SMS] clear all message at init\r\n");
    send_at("AT+CMGD=1,4\r\n");

    
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
    // Bỏ retry - reset luôn về IDLE
    gsm_sms_ctx.state = GSM_SMS_IDLE;
    gsm_sms_reset_flow();
    gsm_sms_ctx.retry_count = 0;
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
                if (is_timeout(gsm_sms_ctx.time_stamp, 200)){
                    uart_send_string(SIM_UART, gsm_sms_ctx.send.text,
                                     (uint16_t)strlen(gsm_sms_ctx.send.text));
                    delay_ms(20); 
                    uart_send_byte(SIM_UART,0x1A); 
                    gsm_sms_ctx.send.step  = 2;
                    gsm_sms_ctx.time_stamp = get_tick_ms();
                    return true;
                }

                
                if (gsm_send_data_queue_pop(line, sizeof(line))) {
                    log_raw_line(line);
                    if (at_parser_line(line, &sms_urc)) {
                        if (sms_urc.type == URC_CMS_ERROR ||
                            sms_urc.type == URC_ERROR) {
                            gsm_sms_ctx.state = GSM_SMS_IDLE;
                            gsm_sms_reset_flow();
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
        // Pop liên tục cho đến khi nhận đủ +CMGR, SMS text và OK
        while (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
            
            char dbg_line[256];
            snprintf(dbg_line, sizeof(dbg_line), ">>> [SMS] case3: line='%s', len=%u\r\n", line, strlen(line));
            send_debug(dbg_line);
            
            if (at_parser_line(line, &sms_urc)) {
                char dbg_urc[128];
                snprintf(dbg_urc, sizeof(dbg_urc), ">>> [SMS] URC type=%d, text='%s'\r\n", sms_urc.type, sms_urc.text);
                send_debug(dbg_urc);
                
                if (sms_urc.type == URC_CMGR) {
                    strncpy(gsm_sms_ctx.receive.phone, sms_urc.text,
                            sizeof(gsm_sms_ctx.receive.phone) - 1);
                    gsm_sms_ctx.receive.phone[sizeof(gsm_sms_ctx.receive.phone) - 1] = '\0';
                    send_debug(">>> [SMS] CMGR: phone saved\r\n");
                    continue; // Tiếp tục pop line tiếp theo
                }
                
                else if (sms_urc.type == URC_SMS_TEXT) {
                    send_debug(">>> [SMS] SMS_TEXT received\r\n");
                    send_debug(">>> [SMS] SMS_TEXT received\r\n");
                    strncpy(gsm_sms_ctx.receive.text, sms_urc.text,
                            sizeof(gsm_sms_ctx.receive.text) - 1);
                    gsm_sms_ctx.receive.text[sizeof(gsm_sms_ctx.receive.text) - 1] = '\0';

                    char dbg[256];
                    snprintf(dbg, sizeof(dbg), ">>> [SMS] text='%s', phone='%s', target='%s'\r\n", 
                             gsm_sms_ctx.receive.text, gsm_sms_ctx.receive.phone, gsm_sms_ctx.target_phone);
                    send_debug(dbg);

                    if (strcmp(gsm_sms_ctx.receive.phone, gsm_sms_ctx.target_phone) == 0) {
                        char cmd[16];
                        bool got_cmd = false;
                        const char *cmd_src = gsm_sms_ctx.receive.text;

                        const char *p = gsm_sms_ctx.receive.text;
                        while (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') p++;

                        if (*p == '{') {
                            jsmn_parser parser;
                            jsmntok_t tokens[16];
                            jsmn_init(&parser);
                            int r = jsmn_parse(&parser, p, strlen(p), tokens, sizeof(tokens)/sizeof(tokens[0]));
                            if (r >= 0) {
                                for (int i = 1; i < r; i++) {
                                    if (tokens[i].type == JSMN_STRING) {
                                        int klen = tokens[i].end - tokens[i].start;
                                        if (klen == 7 && strncmp(p + tokens[i].start, "message", 7) == 0 && (i + 1) < r) {
                                            jsmntok_t *val = &tokens[i + 1];
                                            int vlen = val->end - val->start;
                                            if (vlen > 0 && vlen < sizeof(cmd)) {
                                                memcpy(cmd, p + val->start, vlen);
                                                cmd[vlen] = '\0';
                                                cmd_src = cmd;
                                                got_cmd = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (!got_cmd) {
                            size_t i = 0, j = strlen(gsm_sms_ctx.receive.text);
                            while (i < j && (gsm_sms_ctx.receive.text[i] == ' ' || gsm_sms_ctx.receive.text[i] == '\r' || 
                                   gsm_sms_ctx.receive.text[i] == '\n' || gsm_sms_ctx.receive.text[i] == '\t' || 
                                   gsm_sms_ctx.receive.text[i] == '\"')) i++;
                            while (j > i && (gsm_sms_ctx.receive.text[j-1] == ' ' || gsm_sms_ctx.receive.text[j-1] == '\r' || 
                                   gsm_sms_ctx.receive.text[j-1] == '\n' || gsm_sms_ctx.receive.text[j-1] == '\t' || 
                                   gsm_sms_ctx.receive.text[j-1] == '\"')) j--;
                            size_t plen = (j > i) ? (j - i) : 0;
                            if (plen > 0 && plen < sizeof(cmd)) {
                                memcpy(cmd, &gsm_sms_ctx.receive.text[i], plen);
                                cmd[plen] = '\0';
                                cmd_src = cmd;
                            }
                        }

                        snprintf(dbg, sizeof(dbg), ">>> [SMS] parsed cmd_src='%s'\r\n", cmd_src);
                        send_debug(dbg);

                        led_queue_cmd_t queue_cmd = LED_QUEUE_CMD_NONE;
                        
                        if (strcmp(cmd_src, "0") == 0 || strcmp(cmd_src, "off") == 0 || strcmp(cmd_src, "OFF") == 0) {
                            queue_cmd = LED_QUEUE_CMD_OFF;
                        } else if (strcmp(cmd_src, "1") == 0 || strcmp(cmd_src, "on") == 0 || strcmp(cmd_src, "ON") == 0) {
                            queue_cmd = LED_QUEUE_CMD_ON;
                        }
                        
                        snprintf(dbg, sizeof(dbg), ">>> [SMS] queue_cmd=%d\r\n", queue_cmd);
                        send_debug(dbg);
                        
                        if (queue_cmd != LED_QUEUE_CMD_NONE) {
                            if (led_queue_push(queue_cmd)) {
                                snprintf(dbg, sizeof(dbg), ">>> [SMS] pushed to LED queue: %d\r\n", queue_cmd);
                                send_debug(dbg);
                                
                                // Gửi SMS response ngay
                                if (queue_cmd == LED_QUEUE_CMD_ON) {
                                    gsm_sms_send(gsm_sms_ctx.receive.phone, "LED ON");
                                } else if (queue_cmd == LED_QUEUE_CMD_OFF) {
                                    gsm_sms_send(gsm_sms_ctx.receive.phone, "LED OFF");
                                }
                            } else {
                                send_debug(">>> [SMS] LED queue full!\r\n");
                            }
                        } else {
                            snprintf(dbg, sizeof(dbg), ">>> [SMS] invalid command: '%s'\r\n", cmd_src);
                            send_debug(dbg);
                        }
                    } else {
                        send_debug(">>> [SMS] ignore non-target sender\r\n");
                    }

                    // Xóa SMS và reset luôn, không retry
                    gsm_sms_ctx.receive.step = 4;
                    gsm_sms_ctx.time_stamp   = get_tick_ms();
                    return true; // Đã xử lý xong, return để chuyển sang step 4
                }
                else if (sms_urc.type == URC_OK) {
                    // Nhận được OK sau khi đã xử lý SMS text
                    send_debug(">>> [SMS] OK received after text\r\n");
                    // Nếu chưa xử lý text thì chuyển sang step 4 để xóa
                    if (gsm_sms_ctx.receive.step == 3) {
                        gsm_sms_ctx.receive.step = 4;
                        gsm_sms_ctx.time_stamp = get_tick_ms();
                        return true;
                    }
                    continue;
                }
                else if (sms_urc.type == URC_ERROR || sms_urc.type == URC_CMS_ERROR) {
                    gsm_sms_ctx.receive.step = 0;
                    gsm_sms_ctx.state = GSM_SMS_IDLE;
                    return false;
                }
                continue; // Tiếp tục pop line tiếp theo
            } else {
                // Không phải URC - có thể là SMS text chưa được parse
                char dbg_not_urc[128];
                snprintf(dbg_not_urc, sizeof(dbg_not_urc), ">>> [SMS] case3: not URC, line='%s'\r\n", line);
                send_debug(dbg_not_urc);
                
                // Thử parse như SMS text nếu đang đợi text và line không phải OK/ERROR
                if (strlen(line) > 0 && line[0] != 'O' && line[0] != 'E' && line[0] != '+' && line[0] != 'A') {
                    // Có thể là SMS text - thử xử lý như SMS_TEXT
                    strncpy(gsm_sms_ctx.receive.text, line,
                            sizeof(gsm_sms_ctx.receive.text) - 1);
                    gsm_sms_ctx.receive.text[sizeof(gsm_sms_ctx.receive.text) - 1] = '\0';
                    
                    send_debug(">>> [SMS] treating as SMS_TEXT (not parsed as URC)\r\n");
                    
                    char dbg[256];
                    snprintf(dbg, sizeof(dbg), ">>> [SMS] text='%s', phone='%s', target='%s'\r\n", 
                             gsm_sms_ctx.receive.text, gsm_sms_ctx.receive.phone, gsm_sms_ctx.target_phone);
                    send_debug(dbg);

                    if (strcmp(gsm_sms_ctx.receive.phone, gsm_sms_ctx.target_phone) == 0) {
                        char cmd[16];
                        bool got_cmd = false;
                        const char *cmd_src = gsm_sms_ctx.receive.text;

                        const char *p = gsm_sms_ctx.receive.text;
                        while (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') p++;

                        if (*p == '{') {
                            jsmn_parser parser;
                            jsmntok_t tokens[16];
                            jsmn_init(&parser);
                            int r = jsmn_parse(&parser, p, strlen(p), tokens, sizeof(tokens)/sizeof(tokens[0]));
                            if (r >= 0) {
                                for (int i = 1; i < r; i++) {
                                    if (tokens[i].type == JSMN_STRING) {
                                        int klen = tokens[i].end - tokens[i].start;
                                        if (klen == 7 && strncmp(p + tokens[i].start, "message", 7) == 0 && (i + 1) < r) {
                                            jsmntok_t *val = &tokens[i + 1];
                                            int vlen = val->end - val->start;
                                            if (vlen > 0 && vlen < sizeof(cmd)) {
                                                memcpy(cmd, p + val->start, vlen);
                                                cmd[vlen] = '\0';
                                                cmd_src = cmd;
                                                got_cmd = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (!got_cmd) {
                            size_t i = 0, j = strlen(gsm_sms_ctx.receive.text);
                            while (i < j && (gsm_sms_ctx.receive.text[i] == ' ' || gsm_sms_ctx.receive.text[i] == '\r' || 
                                   gsm_sms_ctx.receive.text[i] == '\n' || gsm_sms_ctx.receive.text[i] == '\t' || 
                                   gsm_sms_ctx.receive.text[i] == '\"')) i++;
                            while (j > i && (gsm_sms_ctx.receive.text[j-1] == ' ' || gsm_sms_ctx.receive.text[j-1] == '\r' || 
                                   gsm_sms_ctx.receive.text[j-1] == '\n' || gsm_sms_ctx.receive.text[j-1] == '\t' || 
                                   gsm_sms_ctx.receive.text[j-1] == '\"')) j--;
                            size_t plen = (j > i) ? (j - i) : 0;
                            if (plen > 0 && plen < sizeof(cmd)) {
                                memcpy(cmd, &gsm_sms_ctx.receive.text[i], plen);
                                cmd[plen] = '\0';
                                cmd_src = cmd;
                            }
                        }

                        snprintf(dbg, sizeof(dbg), ">>> [SMS] parsed cmd_src='%s'\r\n", cmd_src);
                        send_debug(dbg);

                        led_queue_cmd_t queue_cmd = LED_QUEUE_CMD_NONE;
                        
                        if (strcmp(cmd_src, "0") == 0 || strcmp(cmd_src, "off") == 0 || strcmp(cmd_src, "OFF") == 0) {
                            queue_cmd = LED_QUEUE_CMD_OFF;
                        } else if (strcmp(cmd_src, "1") == 0 || strcmp(cmd_src, "on") == 0 || strcmp(cmd_src, "ON") == 0) {
                            queue_cmd = LED_QUEUE_CMD_ON;
                        }
                        
                        snprintf(dbg, sizeof(dbg), ">>> [SMS] queue_cmd=%d\r\n", queue_cmd);
                        send_debug(dbg);
                        
                        if (queue_cmd != LED_QUEUE_CMD_NONE) {
                            if (led_queue_push(queue_cmd)) {
                                snprintf(dbg, sizeof(dbg), ">>> [SMS] pushed to LED queue: %d\r\n", queue_cmd);
                                send_debug(dbg);
                                
                                if (queue_cmd == LED_QUEUE_CMD_ON) {
                                    gsm_sms_send(gsm_sms_ctx.receive.phone, "LED ON");
                                } else if (queue_cmd == LED_QUEUE_CMD_OFF) {
                                    gsm_sms_send(gsm_sms_ctx.receive.phone, "LED OFF");
                                }
                            } else {
                                send_debug(">>> [SMS] LED queue full!\r\n");
                            }
                        } else {
                            snprintf(dbg, sizeof(dbg), ">>> [SMS] invalid command: '%s'\r\n", cmd_src);
                            send_debug(dbg);
                        }
                    } else {
                        send_debug(">>> [SMS] ignore non-target sender\r\n");
                    }

                    gsm_sms_ctx.receive.step = 4;
                    gsm_sms_ctx.time_stamp   = get_tick_ms();
                    return true; // Đã xử lý xong, return để chuyển sang step 4
                }
            }
        }
        // Nếu không còn line trong queue, kiểm tra timeout
        if (is_timeout(gsm_sms_ctx.time_stamp, 5000)) {
            gsm_sms_ctx.receive.step = 0;
            gsm_sms_ctx.state = GSM_SMS_IDLE;
            return false;
        }
        return true; // Tiếp tục đợi line tiếp theo
        
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
        if (is_timeout(gsm_sms_ctx.time_stamp, 1000)) {
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


