#ifndef __GSM_SMS_H__
#define __GSM_SMS_H__

#include <stdint.h>
#include <stdbool.h>
#include "gsm/gsm.h"
#include "gsm/urc/urc.h"



typedef enum {
    GSM_SMS_IDLE = 0,
    GSM_SMS_SEND,
    GSM_SMS_RECEIVE,
    GSM_SMS_READ,
    GSM_SMS_DONE,
    GSM_SMS_ERROR
} gsm_sms_state_t;


typedef struct {
    uint8_t  step;
    uint32_t t_start;
    uint8_t  retry;
    char     phone[16];
    char     text[161];
} gsm_sms_send_ctx_t;


typedef struct {
    uint8_t  step;
    uint32_t t_start;
    uint8_t  index;
    char     phone[16];
    char     text[161];
} gsm_sms_receive_ctx_t;

typedef struct {
    gsm_sms_state_t state;
    uint32_t        time_stamp;
    uint32_t        timeout_ms;
    uint8_t         retry_count;

    char            target_phone[16];
    bool            target_valid;

    gsm_sms_send_ctx_t    send;
    gsm_sms_receive_ctx_t receive;
} gsm_sms_ctx_t;


void gsm_sms_init(void);
void gsm_sms_process(void);
bool gsm_sms_send(const char *phone, const char *text);
bool gsm_sms_has_new(void);
bool gsm_sms_set_target(const char *phone);
const char* gsm_sms_get_target(void);
bool gsm_sms_phase_send(void);
bool gsm_sms_reciv(void);
#endif //__GSM_SMS_H__
