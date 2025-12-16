#ifndef __GSM_NW_H__
#define __GSM_NW_H__

#include <stdint.h>
#include <stdbool.h>
#include "hardware.h"
#include "gsm/gsm.h"
#include "../gsm_send_data_queue.h"
#include "../urc/urc.h"
#include "gsm/gsm_nw/gsm_nw.h"

typedef enum{
    GSM_NW_BASIC = 0,
    GSM_NW_LTE,
    GSM_NW_DONE,
    GSM_NW_ERROR,
    GSM_NW_RESET_SIM
}gsm_nw_t;


typedef struct {
    uint8_t  step;
    uint32_t t_start;
    uint8_t  retry;
} gsm_basic_ctx_t;

typedef struct {
    uint8_t  step;
    uint32_t t_start;
    uint8_t  retry;
    bool     need_data;
    int      mcc;
    int      mnc;
    char     apn[32];  
} gsm_lte_ctx_t;


typedef struct {
    int mcc;
    int mnc;
    const char *apn;
} apn_map_t;



typedef struct {
    uint32_t time_stamp;
    uint32_t timeout_ms; 
    gsm_nw_t state;
    uint8_t  retry_count;     
    gsm_basic_ctx_t basic;
    gsm_lte_ctx_t   lte;
} gsm_net_ctx_t;

void gsm_nw_init(void);
void gsm_nw_process(void);

#endif // __GSM_NW_H__
