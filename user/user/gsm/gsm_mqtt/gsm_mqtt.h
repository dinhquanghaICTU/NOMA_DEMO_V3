#ifndef __GSM_MQTT_H__
#define __GSM_MQTT_H__

#include "stdint.h"
#include "stdbool.h"
#include "string.h"
#include "gsm/gsm.h"
#include "../gsm_send_data_queue.h"
#include "../urc/urc.h"

typedef enum {
    MQTT_PHASE_STOP,
    MQTT_PHASE_START,
    MQTT_PHASE_ACCQ,
    MQTT_PHASE_SSL_VER,
    MQTT_PHASE_SSL_SNI,
    MQTT_PHASE_SSL_AUTH,
    MQTT_PHASE_SSL_TLS,
    MQTT_PHASE_CONN,
    MQTT_PHASE_SUB,
    MQTT_PHASE_IDLE,
    MQTT_PHASE_ERROR
} gsm_mqtt_t;

typedef struct {
    char     broker_url[128];
    uint16_t port;
    char     client_id[64];
    char     username[64];
    char     password[64];
    uint16_t keep_alive;
    uint8_t  client_index;
    bool     use_ssl;
} mqtt_config_t;

typedef struct {
    char     topic[128];
    uint8_t *payload;
    uint16_t payload_len;
    uint8_t  qos;
    uint8_t  retain;
} mqtt_message_t;

typedef void (*gsm_mqtt_cb_t)(void);
typedef void (*gsm_mqtt_msg_cb_t)(const char *topic, const uint8_t *payload, uint16_t len);

typedef struct {
    uint8_t          step;
    gsm_mqtt_t       phase;
    mqtt_config_t    config;
    mqtt_message_t   message;
    uint8_t          retry;
    uint32_t         time_stamp;
    uint32_t         timeout_ms;
    bool             is_connected;
    gsm_mqtt_cb_t    on_mqtt_done;
    gsm_mqtt_cb_t    on_mqtt_error;
    gsm_mqtt_msg_cb_t on_mqtt_message;
} gsm_mqtt_context_t;

extern gsm_mqtt_context_t gsm_mqtt_ctx;

void gsm_mqtt_init(gsm_mqtt_cb_t mqtt_done_cb, gsm_mqtt_cb_t mqtt_error_cb);
void gsm_mqtt_config(const char *broker_url_full, const char *client_id, const char *username, const char *password);
void gsm_mqtt_set_message_callback(gsm_mqtt_msg_cb_t callback);
void gsm_mqtt_subscribe(const char *topic, uint8_t qos);
void gsm_mqtt_publish(const char *topic, const char *payload, uint8_t qos);
bool gsm_mqtt_is_connected(void);
void gsm_mqtt_process(void);
void gsm_mqtt_handle_urc(const char *line);

#endif // __GSM_MQTT_H__
