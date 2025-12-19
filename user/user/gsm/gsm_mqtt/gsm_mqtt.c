#include "gsm_mqtt.h"
#include "led/led.h"
#include "led/led_queue.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "jsmn/jsmn.h"

#define TIME_OUT    5000

#define MQTT_URL                "a907cd687c3b4309adeb32fd3bf1c2b6.s1.eu.hivemq.cloud:8883"
#define MQTT_USER               "quanghaictu"
#define MQTT_PASS               "Quangha123456"
#define MQTT_CONTROL_TOPIC      "DV_test_led"
#define MQTT_RESPONSE_TOPIC     "DV_test_led_response"

gsm_mqtt_context_t gsm_mqtt_ctx;
char line[200];
urc_t urc;



static bool mqtt_extract_message_field(const char *json, char *out, size_t out_len) {
    jsmn_parser p;
    jsmntok_t tokens[16];
    jsmn_init(&p);
    int r = jsmn_parse(&p, json, strlen(json), tokens, sizeof(tokens)/sizeof(tokens[0]));
    if (r < 0) {
        return false;
    }
    for (int i = 1; i < r; i++) {
        if (tokens[i].type == JSMN_STRING) {
            int klen = tokens[i].end - tokens[i].start;
            if (klen == 7 && strncmp(json + tokens[i].start, "message", 7) == 0 && (i + 1) < r) {
                jsmntok_t *val = &tokens[i + 1];
                int vlen = val->end - val->start;
                if (vlen <= 0 || (size_t)vlen >= out_len) {
                    return false;
                }
                memcpy(out, json + val->start, vlen);
                out[vlen] = '\0';
                return true;
            }
        }
    }
    return false;
}

static void mqtt_led_message_handler(const char *topic, const uint8_t *payload, uint16_t len) {
    if (!topic || !payload || len == 0) {
        return;
    }

    char dbg[256];
    snprintf(dbg, sizeof(dbg), ">>> MQTT MSG RAW: topic=%s, payload=%.*s, len=%u\r\n", 
             topic, (int)len, (const char*)payload, len);
    send_debug(dbg);

    if (strcmp(topic, MQTT_CONTROL_TOPIC) != 0) {
        send_debug(">>> MQTT: topic khÃ´ng khá»›p, bá»� qua\r\n");
        return;
    }

    
    char buf[128];
    uint16_t copy_len = (len < sizeof(buf) - 1) ? len : (sizeof(buf) - 1);
    memcpy(buf, payload, copy_len);
    buf[copy_len] = '\0';

    
    char cmd[16];
    bool got_cmd = false;
    const char *cmd_src = buf;

    
    const char *p = buf;
    while (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t') p++;

    if (*p == '{') {
        if (mqtt_extract_message_field(p, cmd, sizeof(cmd))) {
            cmd_src = cmd;
            got_cmd = true;
        }
    }

    if (!got_cmd) {
        
        size_t i = 0, j = strlen(buf);
        while (i < j && (buf[i] == ' ' || buf[i] == '\r' || buf[i] == '\n' || buf[i] == '\t' || buf[i] == '\"')) i++;
        while (j > i && (buf[j-1] == ' ' || buf[j-1] == '\r' || buf[j-1] == '\n' || buf[j-1] == '\t' || buf[j-1] == '\"')) j--;
        size_t plen = (j > i) ? (j - i) : 0;
        if (plen > 0 && plen < sizeof(cmd)) {
            memcpy(cmd, &buf[i], plen);
            cmd[plen] = '\0';
            cmd_src = cmd;
        }
    }

    led_queue_cmd_t queue_cmd = LED_QUEUE_CMD_NONE;
    
    if (strcmp(cmd_src, "0") == 0 || strcmp(cmd_src, "off") == 0 || strcmp(cmd_src, "OFF") == 0) {
        queue_cmd = LED_QUEUE_CMD_OFF;
    } else if (strcmp(cmd_src, "1") == 0 || strcmp(cmd_src, "on") == 0 || strcmp(cmd_src, "ON") == 0) {
        queue_cmd = LED_QUEUE_CMD_ON;
    }
    
    if (queue_cmd != LED_QUEUE_CMD_NONE) {
        led_queue_push(queue_cmd);
    }
}

void gsm_mqtt_init(gsm_mqtt_cb_t mqtt_done_cb, gsm_mqtt_cb_t mqtt_error_cb) {
    gsm_mqtt_ctx.on_mqtt_done = mqtt_done_cb;
    gsm_mqtt_ctx.on_mqtt_error = mqtt_error_cb;
    gsm_mqtt_ctx.retry = 0;
    gsm_mqtt_ctx.time_stamp = get_tick_ms();
    gsm_mqtt_ctx.phase = MQTT_PHASE_STOP;
    gsm_mqtt_ctx.timeout_ms = 0;
    gsm_mqtt_ctx.is_connected = 0;
    gsm_mqtt_ctx.step = 0;
    gsm_mqtt_ctx.config.broker_url[0] = '\0';
    gsm_mqtt_ctx.config.client_id[0] = '\0';
    gsm_mqtt_ctx.config.keep_alive = 60;
    gsm_mqtt_ctx.config.port = 8883;
    gsm_mqtt_ctx.config.username[0] = '\0';
    gsm_mqtt_ctx.config.password[0] = '\0';
    gsm_mqtt_ctx.message.topic[0] = '\0';
    gsm_mqtt_ctx.message.payload_len = 0;
    gsm_mqtt_ctx.message.payload = NULL;
    gsm_mqtt_ctx.message.qos = 1;
    gsm_mqtt_ctx.message.retain = 0;
    
    
    gsm_mqtt_ctx.on_mqtt_message = mqtt_led_message_handler;
}

static char mqtt_rx_topic[128];
static uint8_t mqtt_rx_payload[256];
static uint16_t mqtt_rx_topic_len = 0;
static uint16_t mqtt_rx_topic_expected = 0;
static uint16_t mqtt_rx_payload_len = 0;
static uint16_t mqtt_rx_payload_expected = 0;
static uint8_t mqtt_rx_state = 0;
static uint32_t mqtt_rx_timeout = 0;

void gsm_mqtt_set_message_callback(gsm_mqtt_msg_cb_t callback) {
    gsm_mqtt_ctx.on_mqtt_message = callback;
}

static void mqtt_rx_read_topic(void) {
    if (mqtt_rx_state != 1) return;
    if (mqtt_rx_topic_len >= mqtt_rx_topic_expected) {
        mqtt_rx_state = 2;
        return;
    }
    
    extern uint16_t uart_sim_available(void);
    extern uint16_t uart_sim_read(uint8_t *buf, uint16_t len);
    
    uint16_t avail = uart_sim_available();
    if (avail == 0) return;
    
    uint16_t remaining = mqtt_rx_topic_expected - mqtt_rx_topic_len;
    uint16_t to_read = (avail < remaining) ? avail : remaining;
    to_read = (to_read < (sizeof(mqtt_rx_topic) - mqtt_rx_topic_len - 1)) ?
              to_read : (sizeof(mqtt_rx_topic) - mqtt_rx_topic_len - 1);
    
    if (to_read > 0) {
        uint16_t read = uart_sim_read((uint8_t *)&mqtt_rx_topic[mqtt_rx_topic_len], to_read);
        mqtt_rx_topic_len += read;
        
        char dbg[128];
        snprintf(dbg, sizeof(dbg), ">>> MQTT RX TOPIC UART: read %u bytes, total=%u/%u, avail=%u\r\n", 
                 read, mqtt_rx_topic_len, mqtt_rx_topic_expected, avail);
        send_debug(dbg);
        
        if (mqtt_rx_topic_len >= mqtt_rx_topic_expected) {
            mqtt_rx_topic[mqtt_rx_topic_len] = '\0';
            mqtt_rx_state = 2;
        }
    }
}

static void mqtt_rx_read_payload(void) {
    if (mqtt_rx_state != 2) return;
    if (mqtt_rx_payload_expected == 0) return;
    if (mqtt_rx_payload_len >= mqtt_rx_payload_expected) return;
    
    extern uint16_t uart_sim_available(void);
    extern uint16_t uart_sim_read(uint8_t *buf, uint16_t len);
    
    uint16_t avail = uart_sim_available();
    if (avail == 0) return;
    
    uint16_t remaining = mqtt_rx_payload_expected - mqtt_rx_payload_len;
    uint16_t to_read = (avail < remaining) ? avail : remaining;
    to_read = (to_read < (sizeof(mqtt_rx_payload) - mqtt_rx_payload_len)) ?
              to_read : (sizeof(mqtt_rx_payload) - mqtt_rx_payload_len);
    
    if (to_read > 0) {
        uint16_t read = uart_sim_read(&mqtt_rx_payload[mqtt_rx_payload_len], to_read);
        mqtt_rx_payload_len += read;
        
        char dbg[128];
        snprintf(dbg, sizeof(dbg), ">>> MQTT RX PAYLOAD UART: read %u bytes, total=%u/%u, avail=%u\r\n", 
                 read, mqtt_rx_payload_len, mqtt_rx_payload_expected, avail);
        send_debug(dbg);
        
        if (mqtt_rx_payload_len >= mqtt_rx_payload_expected) {
            mqtt_rx_payload[mqtt_rx_payload_len] = '\0';
        }
    }
}

void gsm_mqtt_handle_urc(const char *line_arg) {
    if (!line_arg) return;
    
    urc_t urc_local;
    bool is_urc = at_parser_line(line_arg, &urc_local);
    
    bool is_mqtt_urc = (is_urc && (
        urc_local.type == URC_CMQTTRXSTART ||
        urc_local.type == URC_CMQTTRXTOPIC ||
        urc_local.type == URC_CMQTTRXPAYLOAD ||
        urc_local.type == URC_CMQTTRXEND ||
        urc_local.type == URC_CMQTTCONNECT ||
        urc_local.type == URC_CMQTTSUB
    ));
    
    if (!is_mqtt_urc && (mqtt_rx_state == 1 || mqtt_rx_state == 2)) {
        if (mqtt_rx_state == 1 && mqtt_rx_topic_expected > 0 && mqtt_rx_topic_len < mqtt_rx_topic_expected) {
            if (strlen(line_arg) > 0 && line_arg[0] != '+' && line_arg[0] != 'A' && line_arg[0] != 'O' && line_arg[0] != 'E') {
                uint16_t line_len = strlen(line_arg);
                uint16_t remaining = mqtt_rx_topic_expected - mqtt_rx_topic_len;
                uint16_t to_copy = (line_len < remaining) ? line_len : remaining;
                to_copy = (to_copy < sizeof(mqtt_rx_topic) - mqtt_rx_topic_len - 1) ?
                         to_copy : (sizeof(mqtt_rx_topic) - mqtt_rx_topic_len - 1);
                memcpy(&mqtt_rx_topic[mqtt_rx_topic_len], line_arg, to_copy);
                mqtt_rx_topic_len += to_copy;
                
                if (mqtt_rx_topic_len >= mqtt_rx_topic_expected) {
                    mqtt_rx_topic[mqtt_rx_topic_len] = '\0';
                    mqtt_rx_state = 2;
                }
            }
            return;
        }
        else if (mqtt_rx_state == 2 && mqtt_rx_payload_expected > 0 && mqtt_rx_payload_len < mqtt_rx_payload_expected) {
            if (strlen(line_arg) > 0 && line_arg[0] != '+' && line_arg[0] != 'A' && line_arg[0] != 'O' && line_arg[0] != 'E') {
                uint16_t line_len = strlen(line_arg);
                uint16_t remaining = mqtt_rx_payload_expected - mqtt_rx_payload_len;
                uint16_t to_copy = (line_len < remaining) ? line_len : remaining;
                to_copy = (to_copy < sizeof(mqtt_rx_payload) - mqtt_rx_payload_len) ?
                         to_copy : (sizeof(mqtt_rx_payload) - mqtt_rx_payload_len);
                memcpy(&mqtt_rx_payload[mqtt_rx_payload_len], line_arg, to_copy);
                mqtt_rx_payload_len += to_copy;
            }
            return;
        }
    }
    
    if (!is_mqtt_urc) {
        return;
    }
    
    if (urc_local.type == URC_CMQTTRXSTART) {
        mqtt_rx_state = 1;
        mqtt_rx_topic_len = 0;
        mqtt_rx_topic_expected = urc_local.v2;
        mqtt_rx_payload_len = 0;
        mqtt_rx_payload_expected = urc_local.v3;
        mqtt_rx_timeout = get_tick_ms();
    }
    else if (urc_local.type == URC_CMQTTRXTOPIC) {
        mqtt_rx_state = 1;
        mqtt_rx_topic_len = 0;
        mqtt_rx_topic_expected = urc_local.v2;
        memset(mqtt_rx_topic, 0, sizeof(mqtt_rx_topic));
    }
    else if (urc_local.type == URC_CMQTTRXPAYLOAD) {
        mqtt_rx_state = 2;
        mqtt_rx_payload_len = 0;
        mqtt_rx_payload_expected = urc_local.v2;
        memset(mqtt_rx_payload, 0, sizeof(mqtt_rx_payload));
    }
    else if (urc_local.type == URC_CMQTTRXEND) {
        if (mqtt_rx_topic_len > 0 && mqtt_rx_payload_len > 0) {
            mqtt_rx_topic[mqtt_rx_topic_len] = '\0';
            mqtt_rx_payload[mqtt_rx_payload_len] = '\0';
            
            char dbg[256];
            snprintf(dbg, sizeof(dbg), ">>> MQTT RX: topic='%.*s', payload='%.*s'\r\n",
                     mqtt_rx_topic_len, mqtt_rx_topic, mqtt_rx_payload_len, mqtt_rx_payload);
            send_debug(dbg);
            
            if (gsm_mqtt_ctx.on_mqtt_message) {
                gsm_mqtt_ctx.on_mqtt_message(mqtt_rx_topic, mqtt_rx_payload, mqtt_rx_payload_len);
            } else {
                send_debug(">>> MQTT: no message callback!\r\n");
            }
        } else {
            send_debug(">>> MQTT RX: incomplete data!\r\n");
        }
        mqtt_rx_state = 0;
        mqtt_rx_topic_len = 0;
        mqtt_rx_payload_len = 0;
    }
}

bool mqtt_phase_stop(void) {
    switch (gsm_mqtt_ctx.step) {
    case 0:
        send_debug(">>> dong phien ket noi truoc\r\n");
        send_at("AT+CMQTTREL=0\r\n");
        gsm_mqtt_ctx.time_stamp = get_tick_ms();
        gsm_mqtt_ctx.step = 1;
        return true;
        
    case 1:

        while (gsm_send_data_queue_pop(line, sizeof(line))) {
            if (at_parser_line(line, &urc)) {
                if (urc.type == URC_OK || urc.type == URC_ERROR) {
                    delete_line(line);
                    gsm_mqtt_ctx.step = 2;
                    gsm_mqtt_ctx.time_stamp = get_tick_ms();
                    return true;
                }
            }
            delete_line(line);
        }
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 3000)) {
            gsm_mqtt_ctx.step = 2;
            gsm_mqtt_ctx.time_stamp = get_tick_ms();
            return true;
        }
        return true;
        
    case 2:

        send_at("AT+CMQTTSTOP\r\n");
        gsm_mqtt_ctx.time_stamp = get_tick_ms();
        gsm_mqtt_ctx.step = 3;
        return true;
        
    case 3:
        while (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
            if (at_parser_line(line, &urc)) {
                if (urc.type == URC_OK) {
                    send_debug(">>> MQTT STOP OK\r\n");
                    gsm_mqtt_ctx.step = 4;
                    gsm_mqtt_ctx.time_stamp = get_tick_ms();
                    delete_line(line);
                    return true;
                }
                else if (urc.type == URC_ERROR) {
                    send_debug(">>> MQTT STOP ERROR (continue anyway)\r\n");
                    gsm_mqtt_ctx.step = 4;
                    gsm_mqtt_ctx.time_stamp = get_tick_ms();
                    delete_line(line);
                    return true;
                }
            }
            delete_line(line);
        }
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 10000)) {
            send_debug(">>> MQTT STOP timeout (continue anyway)\r\n");
            gsm_mqtt_ctx.step = 4;
            gsm_mqtt_ctx.time_stamp = get_tick_ms();
            return true;
        }
        return true;
        
    case 4:
       
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 2000)) {
            send_debug(">>> MQTT STOP: wait for module ready\r\n");
            gsm_mqtt_ctx.step = 0;
            return false;
        }
        return true;
        
    default:
        gsm_mqtt_ctx.step = 0;
        return false;
    }
}

bool mqtt_phase_start(void) {
    switch (gsm_mqtt_ctx.step) {
    case 0:
        send_debug(">>> MQTT START\r\n");
        send_at("AT+CMQTTSTART\r\n");
        gsm_mqtt_ctx.time_stamp = get_tick_ms();
        gsm_mqtt_ctx.step = 1;
        return true;
        
    case 1:
        while (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
            if (at_parser_line(line, &urc)) {
                if (urc.type == URC_OK) {
                    send_debug(">>> MQTT START OK\r\n");
                    gsm_mqtt_ctx.step = 2;
                    gsm_mqtt_ctx.time_stamp = get_tick_ms();
                    delete_line(line);
                    return true;
                }
                else if (urc.type == URC_ERROR) {
                    send_debug(">>> MQTT START ERROR (continue anyway)\r\n");
                    gsm_mqtt_ctx.step = 2;
                    gsm_mqtt_ctx.time_stamp = get_tick_ms();
                    delete_line(line);
                    return true;
                }
            }
            delete_line(line);
        }
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 10000)) {
            gsm_mqtt_ctx.step = 2;
            gsm_mqtt_ctx.time_stamp = get_tick_ms();
            return true;
        }
        return true;
        
    case 2:
        // Delay nhá»� Ä‘á»ƒ Ä‘áº£m báº£o module sáºµn sÃ ng
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 500)) {
            gsm_mqtt_ctx.step = 0;
            return false;
        }
        return true;
        
    default:
        gsm_mqtt_ctx.step = 0;
        return false;
    }
}

bool mqtt_phase_accq(void) {
    switch (gsm_mqtt_ctx.step) {
    case 0:
        send_debug(">>> MQTT ACCQ: release client\r\n");
        send_at("AT+CMQTTREL=0\r\n");
        gsm_mqtt_ctx.time_stamp = get_tick_ms();
        gsm_mqtt_ctx.step = 1;
        return true;
    
    case 1:
        while (gsm_send_data_queue_pop(line, sizeof(line))) {
            if (at_parser_line(line, &urc)) {
                if (urc.type == URC_OK || urc.type == URC_ERROR) {
                    gsm_mqtt_ctx.step = 2;
                    delete_line(line);
                    return true;
                }
            }
            delete_line(line);
        }
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 5000)) {
            gsm_mqtt_ctx.step = 2;
            return true;
        }
        return true;
    
    case 2: {
        char cmd[96];
        if (strlen(gsm_mqtt_ctx.config.client_id) == 0) {
            // Náº¿u chÆ°a cÃ³ client_id, dÃ¹ng default
            strncpy(gsm_mqtt_ctx.config.client_id, "MQTT_Client", sizeof(gsm_mqtt_ctx.config.client_id) - 1);
            gsm_mqtt_ctx.config.client_id[sizeof(gsm_mqtt_ctx.config.client_id) - 1] = '\0';
        }
        snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=0,\"%s\",1\r\n", gsm_mqtt_ctx.config.client_id);
        send_debug(">>> MQTT ACCQ: acquire client\r\n");
        send_at(cmd);
        gsm_mqtt_ctx.time_stamp = get_tick_ms();
        gsm_mqtt_ctx.step = 3;
        return true;
    }
    
    case 3:
        while (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
            if (at_parser_line(line, &urc)) {
                if (urc.type == URC_OK) {
                    send_debug(">>> MQTT ACCQ OK\r\n");
                    gsm_mqtt_ctx.step = 0;
                    delete_line(line);
                    return false;
                }
                else if (urc.type == URC_ERROR || urc.type == URC_CME_ERROR) {
                    send_debug(">>> MQTT ACCQ ERROR\r\n");
                    gsm_mqtt_ctx.phase = MQTT_PHASE_ERROR;
                    gsm_mqtt_ctx.step = 0;
                    delete_line(line);
                    return false;
                }
            }
            delete_line(line);
        }
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 7000)) {
            gsm_mqtt_ctx.phase = MQTT_PHASE_ERROR;
            gsm_mqtt_ctx.step = 0;
            return false;
        }
        return true;
        
    default:
        gsm_mqtt_ctx.step = 0;
        return false;
    }
}

bool mqtt_phase_ssl_ver(void) {
    switch (gsm_mqtt_ctx.step) {
    case 0:
        send_debug(">>> MQTT SSL VER\r\n");
        send_at("AT+CSSLCFG=\"sslversion\",0,3\r\n"); // 3 = TLS 1.2 (4 khÃ´ng há»£p lá»‡)
        gsm_mqtt_ctx.time_stamp = get_tick_ms();
        gsm_mqtt_ctx.step = 1;
        return true;
    
    case 1:
        while (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
            if (at_parser_line(line, &urc)) {
                if (urc.type == URC_OK) {
                    send_debug(">>> MQTT SSL VER OK\r\n");
                    gsm_mqtt_ctx.step = 0;
                    delete_line(line);
                    return false;
                }
                else if (urc.type == URC_ERROR || urc.type == URC_CME_ERROR) {
                    send_debug(">>> MQTT SSL VER ERROR\r\n");
                    gsm_mqtt_ctx.phase = MQTT_PHASE_ERROR;
                    gsm_mqtt_ctx.step = 0;
                    delete_line(line);
                    return false;
                }
            }
            delete_line(line);
        }
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 7000)) {
            gsm_mqtt_ctx.phase = MQTT_PHASE_ERROR;
            gsm_mqtt_ctx.step = 0;
            return false;
        }
        return true;
        
    default:
        gsm_mqtt_ctx.step = 0;
        return false;
    }
}

bool mqtt_phase_ssl_sni(void) {
    switch (gsm_mqtt_ctx.step) {
    case 0:
        send_debug(">>> MQTT SSL SNI\r\n");
        send_at("AT+CSSLCFG=\"enableSNI\",0,1\r\n");
        gsm_mqtt_ctx.time_stamp = get_tick_ms();
        gsm_mqtt_ctx.step = 1;
        return true;
    
    case 1:
        while (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
            if (at_parser_line(line, &urc)) {
                if (urc.type == URC_OK) {
                    send_debug(">>> MQTT SSL SNI OK\r\n");
                    gsm_mqtt_ctx.step = 0;
                    delete_line(line);
                    return false;
                }
                else if (urc.type == URC_ERROR || urc.type == URC_CME_ERROR) {
                    send_debug(">>> MQTT SSL SNI ERROR (continue anyway)\r\n");
                    gsm_mqtt_ctx.step = 0;
                    delete_line(line);
                    return false; // Continue dÃ¹ cÃ³ lá»—i
                }
            }
            delete_line(line);
        }
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 7000)) {
            send_debug(">>> MQTT SSL SNI timeout (continue anyway)\r\n");
            gsm_mqtt_ctx.step = 0;
            return false; // Continue dÃ¹ cÃ³ timeout
        }
        return true;
        
    default:
        gsm_mqtt_ctx.step = 0;
        return false;
    }
}

bool mqtt_phase_ssl_auth(void) {
    switch (gsm_mqtt_ctx.step) {
    case 0: {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "AT+CSSLCFG=\"authmode\",0,0\r\n");
        send_debug(">>> MQTT SSL AUTH\r\n");
        send_at(cmd);
        gsm_mqtt_ctx.time_stamp = get_tick_ms();
        gsm_mqtt_ctx.step = 1;
        return true;
    }
    
    case 1:
        while (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
            if (at_parser_line(line, &urc)) {
                if (urc.type == URC_OK) {
                    send_debug(">>> MQTT SSL AUTH OK\r\n");
                    gsm_mqtt_ctx.step = 2;
                    delete_line(line);
                    return true;
                }
                else if (urc.type == URC_ERROR || urc.type == URC_CME_ERROR) {
                    send_debug(">>> MQTT SSL AUTH ERROR (continue anyway)\r\n");
                    gsm_mqtt_ctx.step = 2;
                    delete_line(line);
                    return true;
                }
            }
            delete_line(line);
        }
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 7000)) {
            send_debug(">>> MQTT SSL AUTH timeout (continue anyway)\r\n");
            gsm_mqtt_ctx.step = 2;
            return true;
        }
        return true;
    
    case 2: {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "AT+CSSLCFG=\"ignorelocaltime\",0,1\r\n");
        send_debug(">>> MQTT SSL IGNORE_TIME\r\n");
        send_at(cmd);
        gsm_mqtt_ctx.time_stamp = get_tick_ms();
        gsm_mqtt_ctx.step = 3;
        return true;
    }
    
    case 3:
        while (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
            if (at_parser_line(line, &urc)) {
                if (urc.type == URC_OK || urc.type == URC_ERROR) {
                    send_debug(">>> MQTT SSL IGNORE_TIME done\r\n");
                    gsm_mqtt_ctx.step = 0;
                    delete_line(line);
                    return false;
                }
            }
            delete_line(line);
        }
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 5000)) {
            send_debug(">>> MQTT SSL IGNORE_TIME timeout (continue anyway)\r\n");
            gsm_mqtt_ctx.step = 0;
            return false;
        }
        return true;
        
    default:
        gsm_mqtt_ctx.step = 0;
        return false;
    }
}

bool mqtt_phase_ssl_tls(void) {
    static uint8_t retry_count = 0;
    switch (gsm_mqtt_ctx.step) {
    case 0:
        // Delay nhá»� Ä‘á»ƒ Ä‘áº£m báº£o session Ä‘Ã£ sáºµn sÃ ng
        gsm_mqtt_ctx.time_stamp = get_tick_ms();
        gsm_mqtt_ctx.step = 1;
        return true;
        
    case 1:
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 500)) {
            gsm_mqtt_ctx.step = 2;
            return true;
        }
        return true;
        
    case 2: {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "AT+CMQTTSSLCFG=0,0\r\n");
        send_debug(">>> MQTT SSL TLS\r\n");
        send_at(cmd);
        gsm_mqtt_ctx.time_stamp = get_tick_ms();
        gsm_mqtt_ctx.step = 3;
        return true;
    }
    
    case 3:
        while (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
            if (at_parser_line(line, &urc)) {
                if (urc.type == URC_OK) {
                    send_debug(">>> MQTT SSL TLS OK\r\n");
                    retry_count = 0;
                    // Delay sau SSL TLS OK để module sẵn sàng
                    gsm_mqtt_ctx.step = 4;
                    gsm_mqtt_ctx.time_stamp = get_tick_ms();
                    delete_line(line);
                    return true;
                }
                else if (urc.type == URC_ERROR || urc.type == URC_CME_ERROR) {
                    retry_count++;
                    if (retry_count < 2) {
                        send_debug(">>> MQTT SSL TLS ERROR, retry...\r\n");
                        gsm_mqtt_ctx.step = 0; // Retry
                        gsm_mqtt_ctx.time_stamp = get_tick_ms();
                        delete_line(line);
                        return true;
                    } else {
                        send_debug(">>> MQTT SSL TLS ERROR (continue anyway)\r\n");
                        retry_count = 0;
                        gsm_mqtt_ctx.step = 0;
                        delete_line(line);
                        return false; // Tiáº¿p tá»¥c dÃ¹ cÃ³ lá»—i
                    }
                }
            }
            delete_line(line);
        }
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 7000)) {
            retry_count++;
            if (retry_count < 2) {
                send_debug(">>> MQTT SSL TLS timeout, retry...\r\n");
                gsm_mqtt_ctx.step = 0;
                gsm_mqtt_ctx.time_stamp = get_tick_ms();
                return true;
            } else {
                send_debug(">>> MQTT SSL TLS timeout (continue anyway)\r\n");
                retry_count = 0;
                gsm_mqtt_ctx.step = 0;
                return false; // Tiáº¿p tá»¥c dÃ¹ cÃ³ timeout
            }
        }
        return true;
        
    case 4:
        // Delay sau SSL TLS OK để module sẵn sàng cho CONNECT
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 2000)) {
            send_debug(">>> MQTT SSL TLS: module ready\r\n");
            retry_count = 0;
            gsm_mqtt_ctx.step = 0;
            return false;
        }
        return true;
        
    default:
        retry_count = 0;
        gsm_mqtt_ctx.step = 0;
        return false;
    }
}

bool mqtt_phase_connect(void) {
    static uint8_t connect_retry = 0;
    switch (gsm_mqtt_ctx.step) {
    case 0:
        // Delay nhá»� Ä‘á»ƒ Ä‘áº£m báº£o SSL config Ä‘Ã£ hoÃ n táº¥t
        gsm_mqtt_ctx.time_stamp = get_tick_ms();
        gsm_mqtt_ctx.step = 1;
        return true;
        
    case 1:
        // Delay lâu hơn để đảm bảo SSL config đã hoàn toàn sẵn sàng (SIM7670C cần thời gian)
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 5000)) {
            gsm_mqtt_ctx.step = 2;
            return true;
        }
        return true;
        
    case 2: {
        // TÃ¡ch hostname vÃ  port tá»« URL
        // Má»™t sá»‘ module SIM chá»‰ nháº­n hostname, khÃ´ng nháº­n port trong URL
        char hostname[128];
        strncpy(hostname, gsm_mqtt_ctx.config.broker_url, sizeof(hostname) - 1);
        hostname[sizeof(hostname) - 1] = '\0';
        

        char *port_pos = strchr(hostname, ':');
        if (port_pos != NULL) {
            *port_pos = '\0';
        }
        
        // Debug: kiểm tra password trước khi format
        char pass_dbg[128];
        snprintf(pass_dbg, sizeof(pass_dbg), ">>> MQTT CONNECT: user='%s', pass='%s', pass_len=%u\r\n", 
                 gsm_mqtt_ctx.config.username, gsm_mqtt_ctx.config.password, strlen(gsm_mqtt_ctx.config.password));
        send_debug(pass_dbg);
        
        char cmd[300]; // Tăng buffer để tránh overflow
        char url_dbg[256];
        snprintf(url_dbg, sizeof(url_dbg), ">>> MQTT CONNECT URL: %s (hostname only)\r\n", hostname);
        send_debug(url_dbg);
        
        // Format URL với tcp:// và port cho SIM7670C
        char full_url[200];
        if (gsm_mqtt_ctx.config.port == 8883) {
            // SSL/TLS: dùng tcp:// với port (module sẽ tự động dùng SSL vì đã config)
            snprintf(full_url, sizeof(full_url), "tcp://%s:%u", hostname, gsm_mqtt_ctx.config.port);
        } else {
            // Non-SSL
            snprintf(full_url, sizeof(full_url), "tcp://%s:%u", hostname, gsm_mqtt_ctx.config.port);
        }
        
        int cmd_len = snprintf(cmd, sizeof(cmd),
            "AT+CMQTTCONNECT=0,\"%s\",%u,%u,\"%s\",\"%s\"\r\n",
            full_url,
            gsm_mqtt_ctx.config.keep_alive,
            1,
            gsm_mqtt_ctx.config.username,
            gsm_mqtt_ctx.config.password);
        
        // Debug: kiểm tra cmd string
        char cmd_dbg[350];
        snprintf(cmd_dbg, sizeof(cmd_dbg), ">>> MQTT CONNECT CMD (len=%d): %s", cmd_len, cmd);
        send_debug(cmd_dbg);
        
        send_debug(">>> MQTT CONNECT\r\n");
        send_at(cmd);
        gsm_mqtt_ctx.time_stamp = get_tick_ms();
        gsm_mqtt_ctx.step = 3;
        return true;
    }
    
    case 3:
        while (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
         
            if (strstr(line, "AT+CMQTTCONNECT") != NULL) {
                delete_line(line);
                continue;
            }
            if (at_parser_line(line, &urc)) {
                if (urc.type == URC_CMQTTCONNECT) {
                    int result = urc.v2;
                    if (result == 0) {
                        send_debug(">>> MQTT CONNECT OK - Connected!\r\n");
                        connect_retry = 0;
                        gsm_mqtt_ctx.is_connected = 1;
                        gsm_mqtt_ctx.phase = MQTT_PHASE_IDLE;
                        gsm_mqtt_ctx.step = 0;
                        delete_line(line);
                        return false;
                    } else {
                        char err_dbg[128];
                        snprintf(err_dbg, sizeof(err_dbg), ">>> MQTT CONNECT ERROR: code=%d\r\n", result);
                        send_debug(err_dbg);
                        
                        // Lá»—i code 32 thÆ°á»�ng lÃ  SSL/TLS issue, thá»­ reset vÃ  retry
                        if (result == 32 && connect_retry < 2) {
                            connect_retry++;
                            send_debug(">>> MQTT CONNECT: SSL error, retry after reset...\r\n");
                            gsm_mqtt_ctx.phase = MQTT_PHASE_STOP; // Reset tá»« Ä‘áº§u
                            gsm_mqtt_ctx.step = 0;
                            delete_line(line);
                            return false;
                        } else {
                            connect_retry = 0;
                            gsm_mqtt_ctx.phase = MQTT_PHASE_ERROR;
                            gsm_mqtt_ctx.step = 0;
                            delete_line(line);
                            return false;
                        }
                    }
                }
                else if (urc.type == URC_OK) {
                    send_debug(">>> MQTT CONNECT OK received, waiting for URC +CMQTTCONNECT...\r\n");
                    delete_line(line);
                    continue;
                }
                else if (urc.type == URC_ERROR || urc.type == URC_CME_ERROR) {
                    char err_msg[256];
                    if (urc.type == URC_CME_ERROR && urc.v2 > 0) {
                        snprintf(err_msg, sizeof(err_msg), ">>> MQTT CONNECT CME ERROR: code=%lu, msg=%s\r\n", urc.v2, line);
                    } else {
                        snprintf(err_msg, sizeof(err_msg), ">>> MQTT CONNECT ERROR: %s\r\n", line);
                    }
                    send_debug(err_msg);
                    if (connect_retry < 2) {
                        connect_retry++;
                        send_debug(">>> MQTT CONNECT ERROR, retry after delay...\r\n");
                        // Delay trước khi retry
                        gsm_mqtt_ctx.time_stamp = get_tick_ms();
                        gsm_mqtt_ctx.step = 0; // Quay lại case 0 để delay
                        delete_line(line);
                        return true;
                    } else {
                        send_debug(">>> MQTT CONNECT ERROR (max retry reached)\r\n");
                        connect_retry = 0;
                        gsm_mqtt_ctx.phase = MQTT_PHASE_ERROR;
                        gsm_mqtt_ctx.step = 0;
                        delete_line(line);
                        return false;
                    }
                }
            }
            delete_line(line);
        }
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 60000)) {
            if (connect_retry < 2) {
                connect_retry++;
                send_debug(">>> MQTT CONNECT timeout, retry...\r\n");
                gsm_mqtt_ctx.phase = MQTT_PHASE_STOP; // Reset tá»« Ä‘áº§u
                gsm_mqtt_ctx.step = 0;
                return false;
            } else {
                send_debug(">>> MQTT CONNECT timeout (60s)\r\n");
                connect_retry = 0;
                gsm_mqtt_ctx.phase = MQTT_PHASE_ERROR;
                gsm_mqtt_ctx.step = 0;
                return false;
            }
        }
        return true;
        
    default:
        gsm_mqtt_ctx.step = 0;
        return false;
    }
}

bool mqtt_phase_sub(void) {
    switch (gsm_mqtt_ctx.step) {
    case 0: {
        uint16_t topic_len = strlen(gsm_mqtt_ctx.message.topic);
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "AT+CMQTTSUB=0,%u,1\r\n", topic_len);
        send_debug(">>> MQTT SUB\r\n");
        send_at(cmd);
        gsm_mqtt_ctx.time_stamp = get_tick_ms();
        gsm_mqtt_ctx.step = 1;
        return true;
    }
    
    case 1: {
        static bool topic_sent = false;
        if (!topic_sent) {
            send_debug(">>> MQTT SUB topic\r\n");
            send_at(gsm_mqtt_ctx.message.topic);
            send_at("\r\n");
            topic_sent = true;
            gsm_mqtt_ctx.time_stamp = get_tick_ms();
            return true;
        }
        
        while (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
            if (at_parser_line(line, &urc)) {
                if (urc.type == URC_CMQTTSUB) {
                    int result = urc.v2;
                    if (result == 0) {
                        send_debug(">>> MQTT SUB OK - Subscribed!\r\n");
                        gsm_mqtt_ctx.phase = MQTT_PHASE_IDLE;
                        gsm_mqtt_ctx.step = 0;
                        topic_sent = false;
                        delete_line(line);
                        return false;
                    } else {
                        char err_dbg[128];
                        snprintf(err_dbg, sizeof(err_dbg), ">>> MQTT SUB ERROR: code=%d\r\n", result);
                        send_debug(err_dbg);
                        gsm_mqtt_ctx.phase = MQTT_PHASE_ERROR;
                        gsm_mqtt_ctx.step = 0;
                        topic_sent = false;
                        delete_line(line);
                        return false;
                    }
                }
                else if (urc.type == URC_OK) {
                    send_debug(">>> MQTT SUB OK (no URC)\r\n");
                    gsm_mqtt_ctx.phase = MQTT_PHASE_IDLE;
                    gsm_mqtt_ctx.step = 0;
                    topic_sent = false;
                    delete_line(line);
                    return false;
                }
                else if (urc.type == URC_ERROR || urc.type == URC_CME_ERROR) {
                    send_debug(">>> MQTT SUB ERROR\r\n");
                    gsm_mqtt_ctx.phase = MQTT_PHASE_ERROR;
                    gsm_mqtt_ctx.step = 0;
                    topic_sent = false;
                    delete_line(line);
                    return false;
                }
            }
            delete_line(line);
        }
        if (is_timeout(gsm_mqtt_ctx.time_stamp, 5000)) {
            send_debug(">>> MQTT SUB timeout\r\n");
            gsm_mqtt_ctx.phase = MQTT_PHASE_ERROR;
            gsm_mqtt_ctx.step = 0;
            topic_sent = false;
            return false;
        }
        return true;
    }
    
    default:
        gsm_mqtt_ctx.step = 0;
        return false;
    }
}

void gsm_mqtt_process(void) {
    static uint32_t phase_start_time = 0;
    static gsm_mqtt_t last_phase = MQTT_PHASE_STOP;
    static uint32_t last_reset_time = 0;
    
    if (last_phase != gsm_mqtt_ctx.phase) {
        phase_start_time = get_tick_ms();
        last_phase = gsm_mqtt_ctx.phase;
    }
    
    // Không timeout khi đã connected và ở IDLE - chỉ lắng nghe message
    uint32_t timeout_limit = 15000;
    if (gsm_mqtt_ctx.phase == MQTT_PHASE_CONN) {
        timeout_limit = 60000;
    }
    
    // Chỉ timeout khi chưa connected, không timeout ở IDLE khi đã connected
    if (gsm_mqtt_ctx.phase != MQTT_PHASE_IDLE || !gsm_mqtt_ctx.is_connected) {
        if (get_tick_ms() - phase_start_time > timeout_limit) {
            gsm_mqtt_ctx.phase = MQTT_PHASE_STOP;
            gsm_mqtt_ctx.step = 0;
            phase_start_time = get_tick_ms();
            last_phase = MQTT_PHASE_STOP;
            last_reset_time = get_tick_ms();
        }
    }
    
    if (last_reset_time > 0 && get_tick_ms() - last_reset_time < 2000) {
        return;
    }
    last_reset_time = 0;
    
    switch (gsm_mqtt_ctx.phase) {
    case MQTT_PHASE_STOP:
        if (!mqtt_phase_stop()) {
            gsm_mqtt_ctx.phase = MQTT_PHASE_START;
            gsm_mqtt_ctx.step = 0;
            phase_start_time = get_tick_ms();
            last_phase = MQTT_PHASE_START;
        }
        break;
        
    case MQTT_PHASE_START:
        if (!mqtt_phase_start()) {
            gsm_mqtt_ctx.phase = MQTT_PHASE_ACCQ;
            gsm_mqtt_ctx.step = 0;
            phase_start_time = get_tick_ms();
            last_phase = MQTT_PHASE_ACCQ;
        }
        break;
        
    case MQTT_PHASE_ACCQ:
        if (!mqtt_phase_accq()) {
            
            // Check SSL dá»±a trÃªn port, khÃ´ng pháº£i URL (vÃ¬ port Ä‘Ã£ bá»‹ bá»� khá»�i URL)
            bool use_ssl = (gsm_mqtt_ctx.config.port == 8883);
            if (use_ssl) {
                // Thá»© tá»± Ä‘Ãºng: AUTH -> VER -> SNI -> TLS
                gsm_mqtt_ctx.phase = MQTT_PHASE_SSL_AUTH;
            } else {
                gsm_mqtt_ctx.phase = MQTT_PHASE_CONN;
            }
            gsm_mqtt_ctx.step = 0;
            phase_start_time = get_tick_ms();
            last_phase = gsm_mqtt_ctx.phase;
        }
        break;
        
    case MQTT_PHASE_SSL_AUTH:
        if (!mqtt_phase_ssl_auth()) {
            // Sau AUTH má»›i Ä‘áº¿n VER
            gsm_mqtt_ctx.phase = MQTT_PHASE_SSL_VER;
            gsm_mqtt_ctx.step = 0;
            phase_start_time = get_tick_ms();
            last_phase = MQTT_PHASE_SSL_VER;
        }
        break;
        
    case MQTT_PHASE_SSL_VER:
        if (!mqtt_phase_ssl_ver()) {
            // Sau VER má»›i Ä‘áº¿n SNI
            gsm_mqtt_ctx.phase = MQTT_PHASE_SSL_SNI;
            gsm_mqtt_ctx.step = 0;
            phase_start_time = get_tick_ms();
            last_phase = MQTT_PHASE_SSL_SNI;
        }
        break;
        
    case MQTT_PHASE_SSL_SNI:
        if (!mqtt_phase_ssl_sni()) {
            // Sau SNI má»›i Ä‘áº¿n TLS (CMQTTSSLCFG) - CUá»�I CÃ™NG
            gsm_mqtt_ctx.phase = MQTT_PHASE_SSL_TLS;
            gsm_mqtt_ctx.step = 0;
            phase_start_time = get_tick_ms();
            last_phase = MQTT_PHASE_SSL_TLS;
        }
        break;
        
    case MQTT_PHASE_SSL_TLS:
        if (!mqtt_phase_ssl_tls()) {
            // Sau TLS má»›i Ä‘áº¿n CONNECT
            gsm_mqtt_ctx.phase = MQTT_PHASE_CONN;
            gsm_mqtt_ctx.step = 0;
            phase_start_time = get_tick_ms();
            last_phase = MQTT_PHASE_CONN;
        }
        break;
        
    case MQTT_PHASE_CONN:
        if (!mqtt_phase_connect()) {
            if (gsm_mqtt_ctx.is_connected) {
                if (strlen(gsm_mqtt_ctx.message.topic) > 0) {
                    gsm_mqtt_ctx.phase = MQTT_PHASE_SUB;
                } else {
                    gsm_mqtt_ctx.phase = MQTT_PHASE_IDLE;
                }
                gsm_mqtt_ctx.step = 0;
                phase_start_time = get_tick_ms();
                last_phase = gsm_mqtt_ctx.phase;
            }
        }
        break;
        
    case MQTT_PHASE_SUB:
        if (!mqtt_phase_sub()) {
            gsm_mqtt_ctx.phase = MQTT_PHASE_IDLE;
            gsm_mqtt_ctx.step = 0;
            phase_start_time = get_tick_ms();
            last_phase = MQTT_PHASE_IDLE;
        }
        break;
        
    case MQTT_PHASE_IDLE:
        if (!gsm_mqtt_ctx.is_connected) {
            gsm_mqtt_ctx.phase = MQTT_PHASE_STOP;
            gsm_mqtt_ctx.step = 0;
            phase_start_time = get_tick_ms();
            last_phase = MQTT_PHASE_STOP;
        } else {
            // Ping MQTT định kỳ để giữ kết nối (mỗi keep_alive/2 giây, tối thiểu 30 giây)
            static uint32_t last_ping_time = 0;
            static bool ping_waiting_response = false;
            uint32_t ping_interval = (gsm_mqtt_ctx.config.keep_alive > 0) ? 
                                     (gsm_mqtt_ctx.config.keep_alive * 500) : 30000; // keep_alive/2 giây hoặc 30s
            if (ping_interval < 30000) ping_interval = 30000; // Tối thiểu 30 giây
            
            // Xử lý response của ping nếu đang đợi
            if (ping_waiting_response) {
                char line_ping[200];
                urc_t urc_ping;
                while (gsm_send_data_queue_pop(line_ping, sizeof(line_ping))) {
                    if (at_parser_line(line_ping, &urc_ping)) {
                        if (urc_ping.type == URC_OK) {
                            send_debug(">>> MQTT PING OK\r\n");
                            ping_waiting_response = false;
                            delete_line(line_ping);
                            break;
                        } else if (urc_ping.type == URC_ERROR) {
                            send_debug(">>> MQTT PING ERROR (ignored)\r\n");
                            ping_waiting_response = false;
                            delete_line(line_ping);
                            break;
                        }
                    }
                    delete_line(line_ping);
                }
            }
            
            // Gửi ping mới nếu đã đến lúc và không đang đợi response
            if (!ping_waiting_response && (last_ping_time == 0 || get_tick_ms() - last_ping_time > ping_interval)) {
                char cmd[32];
                snprintf(cmd, sizeof(cmd), "AT+CMQTTPING=0\r\n");
                send_at(cmd);
                last_ping_time = get_tick_ms();
                ping_waiting_response = true;
                send_debug(">>> MQTT PING sent\r\n");
            }
            
            // Lắng nghe message từ queue - xử lý TẤT CẢ line, kể cả không phải URC
            while (gsm_send_data_queue_pop(line, sizeof(line))) {
                log_raw_line(line);
                // Luôn gọi handle_urc để xử lý cả URC và data (topic/payload)
                gsm_mqtt_handle_urc(line);
            }

            // Đọc topic và payload từ UART nếu đang nhận message
            mqtt_rx_read_topic();
            mqtt_rx_read_payload();
        }
        break;
        
    case MQTT_PHASE_ERROR:
        {
            static uint32_t error_time = 0;
            if (error_time == 0) {
                error_time = get_tick_ms();
            }
            if (get_tick_ms() - error_time > 3000) {
                
                send_debug(">>> MQTT ERROR: go to IDLE\r\n");
                gsm_mqtt_ctx.phase = MQTT_PHASE_IDLE;
                gsm_mqtt_ctx.step = 0;
                phase_start_time = get_tick_ms();
                last_phase = MQTT_PHASE_IDLE;
                error_time = 0;
            }
        }
        break;
        
    default:
        gsm_mqtt_ctx.phase = MQTT_PHASE_STOP;
        gsm_mqtt_ctx.step = 0;
        phase_start_time = get_tick_ms();
        last_phase = MQTT_PHASE_STOP;
        break;
    }
}

void gsm_mqtt_config(const char *broker_url_full, const char *client_id, const char *username, const char *password) {
    if (broker_url_full) {
        strncpy(gsm_mqtt_ctx.config.broker_url, broker_url_full, sizeof(gsm_mqtt_ctx.config.broker_url) - 1);
        gsm_mqtt_ctx.config.broker_url[sizeof(gsm_mqtt_ctx.config.broker_url) - 1] = '\0';
    } else {
        strncpy(gsm_mqtt_ctx.config.broker_url, MQTT_URL, sizeof(gsm_mqtt_ctx.config.broker_url) - 1);
        gsm_mqtt_ctx.config.broker_url[sizeof(gsm_mqtt_ctx.config.broker_url) - 1] = '\0';
    }
    

    // Parse port tá»« URL (tÃ¬m :port sau hostname, bá»� qua :// trong protocol)
    char *url_work = gsm_mqtt_ctx.config.broker_url;
    char *hostname_start = strstr(url_work, "://");
    if (hostname_start) {
        hostname_start += 3; // Bá»� qua "://"
    } else {
        hostname_start = url_work;
    }
    
    // TÃ¬m port sau hostname
    char *port_pos = strchr(hostname_start, ':');
    if (port_pos != NULL && port_pos[1] != '\0') {
        int port = atoi(port_pos + 1);
        if (port > 0 && port < 65536) {
            gsm_mqtt_ctx.config.port = port;
        }
        // Bá»� port khá»�i URL (module SIM khÃ´ng nháº­n port trong URL)
        *port_pos = '\0';
    }
    
    // Bá»� protocol prefix (náº¿u cÃ³)
    if (strncmp(gsm_mqtt_ctx.config.broker_url, "ssl://", 6) == 0) {
        char temp_url[128];
        strncpy(temp_url, gsm_mqtt_ctx.config.broker_url, sizeof(temp_url) - 1);
        temp_url[sizeof(temp_url) - 1] = '\0';
        strncpy(gsm_mqtt_ctx.config.broker_url, temp_url + 6, sizeof(gsm_mqtt_ctx.config.broker_url) - 1);
        gsm_mqtt_ctx.config.broker_url[sizeof(gsm_mqtt_ctx.config.broker_url) - 1] = '\0';
    }
    else if (strncmp(gsm_mqtt_ctx.config.broker_url, "tls://", 6) == 0) {
        char temp_url[128];
        strncpy(temp_url, gsm_mqtt_ctx.config.broker_url, sizeof(temp_url) - 1);
        temp_url[sizeof(temp_url) - 1] = '\0';
        strncpy(gsm_mqtt_ctx.config.broker_url, temp_url + 6, sizeof(gsm_mqtt_ctx.config.broker_url) - 1);
        gsm_mqtt_ctx.config.broker_url[sizeof(gsm_mqtt_ctx.config.broker_url) - 1] = '\0';
    }
    else if (strncmp(gsm_mqtt_ctx.config.broker_url, "tcp://", 6) == 0) {
        char temp_url[128];
        strncpy(temp_url, gsm_mqtt_ctx.config.broker_url, sizeof(temp_url) - 1);
        temp_url[sizeof(temp_url) - 1] = '\0';
        strncpy(gsm_mqtt_ctx.config.broker_url, temp_url + 6, sizeof(gsm_mqtt_ctx.config.broker_url) - 1);
        gsm_mqtt_ctx.config.broker_url[sizeof(gsm_mqtt_ctx.config.broker_url) - 1] = '\0';
    }
    
    if (client_id) {
        strncpy(gsm_mqtt_ctx.config.client_id, client_id, sizeof(gsm_mqtt_ctx.config.client_id) - 1);
        gsm_mqtt_ctx.config.client_id[sizeof(gsm_mqtt_ctx.config.client_id) - 1] = '\0';
    }
    
    if (username) {
        strncpy(gsm_mqtt_ctx.config.username, username, sizeof(gsm_mqtt_ctx.config.username) - 1);
        gsm_mqtt_ctx.config.username[sizeof(gsm_mqtt_ctx.config.username) - 1] = '\0';
    } else {
        strncpy(gsm_mqtt_ctx.config.username, MQTT_USER, sizeof(gsm_mqtt_ctx.config.username) - 1);
        gsm_mqtt_ctx.config.username[sizeof(gsm_mqtt_ctx.config.username) - 1] = '\0';
    }
    
    if (password) {
        strncpy(gsm_mqtt_ctx.config.password, password, sizeof(gsm_mqtt_ctx.config.password) - 1);
        gsm_mqtt_ctx.config.password[sizeof(gsm_mqtt_ctx.config.password) - 1] = '\0';
    } else {
        strncpy(gsm_mqtt_ctx.config.password, MQTT_PASS, sizeof(gsm_mqtt_ctx.config.password) - 1);
        gsm_mqtt_ctx.config.password[sizeof(gsm_mqtt_ctx.config.password) - 1] = '\0';
    }
    
    strncpy(gsm_mqtt_ctx.message.topic, MQTT_CONTROL_TOPIC, sizeof(gsm_mqtt_ctx.message.topic) - 1);
    gsm_mqtt_ctx.message.topic[sizeof(gsm_mqtt_ctx.message.topic) - 1] = '\0';
    gsm_mqtt_ctx.message.qos = 1;
}

void gsm_mqtt_subscribe(const char *topic, uint8_t qos) {
    if (!gsm_mqtt_ctx.is_connected) {
        strncpy(gsm_mqtt_ctx.message.topic, topic, sizeof(gsm_mqtt_ctx.message.topic) - 1);
        gsm_mqtt_ctx.message.topic[sizeof(gsm_mqtt_ctx.message.topic) - 1] = '\0';
        gsm_mqtt_ctx.message.qos = qos;
        return;
    }
    
    strncpy(gsm_mqtt_ctx.message.topic, topic, sizeof(gsm_mqtt_ctx.message.topic) - 1);
    gsm_mqtt_ctx.message.topic[sizeof(gsm_mqtt_ctx.message.topic) - 1] = '\0';
    gsm_mqtt_ctx.message.qos = qos;
    gsm_mqtt_ctx.phase = MQTT_PHASE_SUB;
    gsm_mqtt_ctx.step = 0;
}

bool gsm_mqtt_is_connected(void) {
    return gsm_mqtt_ctx.is_connected;
}

void gsm_mqtt_publish(const char *topic, const char *payload, uint8_t qos) {
    if (!gsm_mqtt_ctx.is_connected || !topic || !payload) {
        send_debug(">>> MQTT PUB: not connected or invalid params\r\n");
        return;
    }
    
    uint16_t topic_len = strlen(topic);
    uint16_t payload_len = strlen(payload);
    
    char cmd[128];
    char line_local[200];
    urc_t urc_local;
    uint32_t timeout;
    
    send_debug(">>> MQTT PUB\r\n");
    
    snprintf(cmd, sizeof(cmd), "AT+CMQTTTOPIC=0,%u\r\n", topic_len);
    send_at(cmd);
    timeout = get_tick_ms();
    while (get_tick_ms() - timeout < 3000) {
        if (gsm_send_data_queue_pop(line_local, sizeof(line_local))) {
            if (at_parser_line(line_local, &urc_local)) {
                if (urc_local.type == URC_OK) {
                    delete_line(line_local);
                    break;
                } else if (urc_local.type == URC_ERROR) {
                    delete_line(line_local);
                    return;
                }
            }
            delete_line(line_local);
        }
    }
    
    send_at(topic);
    send_at("\r\n");
    delay_ms(100);
    
    snprintf(cmd, sizeof(cmd), "AT+CMQTTPAYLOAD=0,%u\r\n", payload_len);
    send_at(cmd);
    timeout = get_tick_ms();
    while (get_tick_ms() - timeout < 3000) {
        if (gsm_send_data_queue_pop(line_local, sizeof(line_local))) {
            if (at_parser_line(line_local, &urc_local)) {
                if (urc_local.type == URC_OK) {
                    delete_line(line_local);
                    break;
                } else if (urc_local.type == URC_ERROR) {
                    delete_line(line_local);
                    return;
                }
            }
            delete_line(line_local);
        }
    }
    
    send_at(payload);
    send_at("\r\n");
    delay_ms(100);
    
    snprintf(cmd, sizeof(cmd), "AT+CMQTTPUB=0,%u,0\r\n", qos);
    send_at(cmd);
    timeout = get_tick_ms();
    while (get_tick_ms() - timeout < 5000) {
        if (gsm_send_data_queue_pop(line_local, sizeof(line_local))) {
            if (at_parser_line(line_local, &urc_local)) {
                if (urc_local.type == URC_OK) {
                    send_debug(">>> MQTT PUB: OK - Published!\r\n");
                    delete_line(line_local);
                    return;
                } else if (urc_local.type == URC_ERROR) {
                    send_debug(">>> MQTT PUB: ERROR\r\n");
                    delete_line(line_local);
                    return;
                }
            }
            delete_line(line_local);
        }
    }
    send_debug(">>> MQTT PUB: timeout\r\n");
}
