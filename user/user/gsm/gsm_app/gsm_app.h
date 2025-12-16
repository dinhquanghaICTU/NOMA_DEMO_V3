#ifndef __GSM_APP_H__
#define __GSM_APP_H__ 

/*
    include 
*/
#include "stdint.h"
#include "string.h"
#include "hardware.h"
#include "stdbool.h"
#include "driver/uart/uart.h"
#include "driver/hardware.h"
#include "gsm/gsm_send_data_queue.h"
#include "gsm/gsm_nw/gsm_nw.h"


/*
    struct, enum
*/
typedef enum {
    APP_BOOT = 0,           
    APP_WAIT_NET,           
    APP_IDLE,               
    APP_HANDLE_SMS,         
    APP_HANDLE_HTTP,        
    APP_HANDLE_MQTT,        
    APP_ERROR               
} app_state_t;

typedef struct {
    app_state_t state;              
    
    
    uint32_t last_sim_check;         
    uint32_t time_stamp;             
    
    
    bool        cpin_pending;
    uint32_t    cpin_start;
    uint32_t    cpin_last;
    
    bool sim_ok;                     
    
    
    uint8_t retry_count;             
    uint32_t error_time;             
    
} app_ctx_t;




typedef struct {
    uint8_t  step;      
    uint32_t t_start;   
} sim_check_ctx_t;
/*
    funcion
*/


void app_init(void);
void app_process(void);
void app_reset_all(void);
bool app_is_network_ready(void);

#endif // __GSM_APP_H__