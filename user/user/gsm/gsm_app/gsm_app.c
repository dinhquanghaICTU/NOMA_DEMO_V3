#include "gsm_app.h"
#include <string.h>
#include "../gsm_sms/gsm_sms.h"
#include "../gsm_mqtt/gsm_mqtt.h"
#include "driver/W25Qx/w25qx.h"
#include "driver/led/led.h"
#include "driver/led/led_queue.h"


static app_ctx_t app_ctx;
extern gsm_net_ctx_t gsm_nw_ctx;
extern gsm_sms_ctx_t gsm_sms_ctx;
extern gsm_mqtt_context_t gsm_mqtt_ctx;
static sim_check_ctx_t sim_check;

static bool test_sms_sent = false;


static bool gsm_sim_check(void){
    return true;
}

void init_hardware(void){
    rcc_config();
    systick_config();
    uart_init();
    w25qxx_init();
    gsm_nw_init();
    led_init();
    led_state_init();
    led_queue_init();  // Init LED command queue
    gsm_sms_init();
    gsm_sms_set_target("+84396674953");
    
    // Init MQTT
    gsm_mqtt_init(NULL, NULL);
    gsm_mqtt_config(NULL, "noma_client_01", NULL, NULL);
}

void app_reset_all(void){
    gsm_nw_init();
    gsm_sms_init();
    gsm_mqtt_init(NULL, NULL);
    gsm_mqtt_config(NULL, "noma_client_01", NULL, NULL);
    test_sms_sent = false;
    app_ctx.sim_ok = true;
    app_ctx.state = APP_WAIT_NET;
    app_ctx.time_stamp = get_tick_ms();
    app_ctx.last_sim_check = get_tick_ms();
}

void app_init(void){
    memset(&app_ctx, 0, sizeof(app_ctx));
    app_ctx.state = APP_BOOT;
    app_ctx.time_stamp = get_tick_ms();
    app_ctx.last_sim_check = get_tick_ms();
    app_ctx.sim_ok = true;
}

static void app_process_led_queue(void) {
    led_queue_cmd_t cmd;
    while (led_queue_pop(&cmd)) {
        char dbg[128];
        snprintf(dbg, sizeof(dbg), ">>> LED QUEUE: pop cmd=%d, count=%u\r\n", cmd, led_queue_count());
        send_debug(dbg);
        
        if (cmd == LED_QUEUE_CMD_OFF) {
            send_debug(">>> LED QUEUE: applying OFF\r\n");
            led_apply_command("0");
            send_debug(">>> LED QUEUE: OFF done\r\n");
        } else if (cmd == LED_QUEUE_CMD_ON) {
            send_debug(">>> LED QUEUE: applying ON\r\n");
            led_apply_command("1");
            send_debug(">>> LED QUEUE: ON done\r\n");
        } else {
            snprintf(dbg, sizeof(dbg), ">>> LED QUEUE: unknown cmd=%d\r\n", cmd);
            send_debug(dbg);
        }
    }
}

static void app_idle_polling(void) {
    char line[30];
    urc_t urc;

    // Check CPIN định kỳ để phát hiện SIM bị rút (mỗi 30 giây)
    // Kiểm tra SMS hoặc MQTT đang busy
    bool sms_busy = (gsm_sms_ctx.state == GSM_SMS_SEND || 
                     (gsm_sms_ctx.state == GSM_SMS_RECEIVE && gsm_sms_ctx.receive.step >= 2));
    bool mqtt_busy = (gsm_mqtt_ctx.phase != MQTT_PHASE_IDLE && gsm_mqtt_ctx.phase != MQTT_PHASE_STOP);
    
    // Check CPIN định kỳ (mỗi 30 giây), chỉ khi không busy
    if (!app_ctx.cpin_pending && !sms_busy && !mqtt_busy && is_timeout(app_ctx.cpin_last, 30000)) {
        send_debug(">>> [SIM] CPIN check\r\n");
        send_at("AT+CPIN?\r\n");
        app_ctx.cpin_pending = true;
        app_ctx.cpin_start   = get_tick_ms();
        app_ctx.cpin_last    = get_tick_ms();
    }

    if (app_ctx.cpin_pending) {
        if (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
            if (at_parser_line(line, &urc)) {
                if (urc.type == URC_CPIN_READY) {
                    app_ctx.sim_ok = true;
                    app_ctx.cpin_pending = false;
                    send_debug(">>> [SIM] CPIN READY\r\n");
                }
                else if (urc.type == URC_OK) {
                    app_ctx.sim_ok = true;
                    app_ctx.cpin_pending = false;
                    send_debug(">>> [SIM] CPIN OK\r\n");
                } else if (urc.type == URC_CPIN_PIN ||
                           urc.type == URC_CPIN_PUK ||
                           urc.type == URC_ERROR) {
                    send_debug(">>> [SIM] CPIN NOT READY - SIM may be removed!\r\n");
                    app_ctx.sim_ok = false;
                    app_ctx.cpin_pending = false;
                    
                    // Nếu MQTT đang connected, ngắt kết nối
                    if (gsm_mqtt_ctx.is_connected) {
                        send_debug(">>> [SIM] Disconnecting MQTT due to SIM issue\r\n");
                        gsm_mqtt_ctx.is_connected = false;
                        gsm_mqtt_ctx.phase = MQTT_PHASE_STOP;
                    }
                    
                    app_ctx.state = APP_WAIT_NET;
                    app_reset_all();
                }
            }
        }

        // Timeout chờ trả lời CPIN (5s)
        if (is_timeout(app_ctx.cpin_start, 5000)) {
            send_debug(">>> [SIM] CPIN timeout - SIM may be removed!\r\n");
            app_ctx.cpin_pending = false;
            app_ctx.sim_ok = false;
            
            // Nếu MQTT đang connected, ngắt kết nối
            if (gsm_mqtt_ctx.is_connected) {
                send_debug(">>> [SIM] Disconnecting MQTT due to SIM timeout\r\n");
                gsm_mqtt_ctx.is_connected = false;
                gsm_mqtt_ctx.phase = MQTT_PHASE_STOP;
            }
            
            app_ctx.state = APP_WAIT_NET;
            app_reset_all();
        }
    }

    // Chỉ gọi SMS receive nếu không đang send và MQTT không busy (phase khác IDLE/STOP)
    {
        bool mqtt_busy = (gsm_mqtt_ctx.phase != MQTT_PHASE_IDLE && gsm_mqtt_ctx.phase != MQTT_PHASE_STOP);
        if (gsm_sms_ctx.state != GSM_SMS_SEND && !mqtt_busy) {
        gsm_sms_reciv();
        }
    }
}

void app_process(void){
    switch (app_ctx.state)
    {
    case APP_BOOT:
        init_hardware();
        app_ctx.state = APP_WAIT_NET;
        break;

    case APP_WAIT_NET:
        gsm_nw_process();
        if (gsm_nw_ctx.state == GSM_NW_DONE) {
            app_ctx.state = APP_IDLE;
        }
        break;

    case APP_IDLE:
        // Polling (CPIN check, SMS receive)
        app_idle_polling();
       
//        if (!test_sms_sent && gsm_sms_ctx.state == GSM_SMS_IDLE && gsm_sms_ctx.target_valid) {
//            gsm_sms_send("0386126985", "test\r\n");
//            test_sms_sent = true;
//        }
        
        // Process LED queue (xử lý commands từ MQTT)
        app_process_led_queue();
        
        // Process SMS (ưu tiên cao hơn)
        gsm_sms_process();
        
        // Process MQTT (ưu tiên thấp hơn SMS)
        // Chỉ chạy MQTT khi SMS không busy
        if (gsm_sms_ctx.state != GSM_SMS_SEND && 
            gsm_sms_ctx.state != GSM_SMS_RECEIVE) {
            gsm_mqtt_process();
        }
        break;
    default:
        break;
    }
}
