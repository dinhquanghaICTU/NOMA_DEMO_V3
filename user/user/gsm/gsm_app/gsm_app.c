#include "gsm_app.h"
#include <string.h>
#include "../gsm_sms/gsm_sms.h"
#include "driver/W25Qx/w25qx.h"


static app_ctx_t app_ctx;
extern gsm_net_ctx_t gsm_nw_ctx;
extern gsm_sms_ctx_t gsm_sms_ctx;
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
    gsm_sms_init();
    gsm_sms_set_target("0837645067");
}

void app_reset_all(void){
    gsm_nw_init();
    gsm_sms_init();
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

static void app_idle_polling(void) {

    if (gsm_sms_ctx.state != GSM_SMS_SEND) {
        gsm_sms_reciv();
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
        app_idle_polling();
       
        if (!test_sms_sent && gsm_sms_ctx.state == GSM_SMS_IDLE && gsm_sms_ctx.target_valid) {
            gsm_sms_send("0837645067", "test\r\n");
            test_sms_sent = true;
        }
        gsm_sms_process();
        break;
    default:
        break;
    }
}
