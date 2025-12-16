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
    APP_BOOT = 0,           // Khởi động, init các module
    APP_WAIT_NET,           // Chờ network DONE
    APP_IDLE,               // Mạng OK, chờ flags
    APP_HANDLE_SMS,         // Đang xử lý SMS
    APP_HANDLE_HTTP,        // Đang xử lý HTTP
    APP_HANDLE_MQTT,        // Đang xử lý MQTT
    APP_ERROR               // Lỗi toàn hệ thống
} app_state_t;

typedef struct {
    app_state_t state;              // State hiện tại
    
    // Timestamps cho các check định kỳ
    uint32_t last_sim_check;         // Lần cuối check SIM (mỗi 5-10s)
    uint32_t time_stamp;             // Timestamp chung cho timeout
    
    // Flags cho các module (set từ URC handler)
    bool sms_pending;                // Có SMS cần xử lý
    bool http_pending;               // Có HTTP cần xử lý
    bool mqtt_pending;               // Có MQTT cần xử lý
    
    bool sim_ok;                     // SIM có OK không
    
    // Retry/error handling
    uint8_t retry_count;             // Số lần retry khi lỗi
    uint32_t error_time;             // Thời điểm lỗi (để auto recover)
    
} app_ctx_t;




typedef struct {
    uint8_t  step;      // 0: idle, 1: đã gửi AT+CPIN?, đang chờ
    uint32_t t_start;   // thời điểm gửi CPIN, để timeout
} sim_check_ctx_t;
/*
    funcion
*/

// API functions
void app_init(void);
void app_process(void);
void app_reset_all(void);
bool app_is_network_ready(void);

#endif // __GSM_APP_H__