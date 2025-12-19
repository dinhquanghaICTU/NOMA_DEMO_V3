#include "gsm_nw.h"
#include <string.h>
#include <stdio.h>
#define TIME_OUT 10000 
 
gsm_net_ctx_t gsm_nw_ctx;
gsm_urc_queue_t get_urc; 
urc_t urc;



const apn_map_t apn_table[] = {
    {452, 1, "m3-world"},      // MobiFone
    {452, 2, "m-wap"},         // VinaPhone
    {452, 4, "v-internet"},    // Viettel
    {452, 5, "v-internet"},    // Viettel (MNC 05)
    {452, 6, "v-internet"},    // Viettel (MNC 06)
    {452, 7, "internet"},      // Gmobile (vÃ­ dá»¥)
    {452, 8, "itel"},          // I-Telecom (vÃ­ dá»¥)
    {452, 9, "reddi"},         // Reddi (vÃ­ dá»¥)
};

void gsm_nw_init(){
    memset(&gsm_nw_ctx, 0, sizeof(gsm_nw_ctx));
    gsm_nw_ctx.state      = GSM_NW_BASIC;
    gsm_nw_ctx.timeout_ms = TIME_OUT;      
    gsm_nw_ctx.retry_count = 0;
    gsm_nw_ctx.basic.step = 0;
    gsm_nw_ctx.lte.step   = 0;
    gsm_nw_ctx.lte.need_data = true; 
}


static void gsm_nw_handle_error(void) {
    gsm_nw_ctx.retry_count++;
    
    if (gsm_nw_ctx.retry_count < 3) {
       
        send_debug(">>> Retry ");
        char retry_str[8];
        snprintf(retry_str, sizeof(retry_str), "%d", gsm_nw_ctx.retry_count);
        send_debug(retry_str);
        send_debug("/3\r\n");
        
        gsm_nw_ctx.state = GSM_NW_BASIC;
        gsm_nw_ctx.basic.step = 0;
        gsm_nw_ctx.lte.step = 0;
    } else {
        
        send_debug(">>> Retry 3 times failed, reset SIM\r\n");
        gsm_nw_ctx.state = GSM_NW_RESET_SIM;
        gsm_nw_ctx.lte.step = 0; 
        gsm_nw_ctx.retry_count = 0;  
    }
}

const char* apn_from_mcc_mnc(int mcc, int mnc) {
    for (size_t i = 0; i < sizeof(apn_table)/sizeof(apn_table[0]); i++) {
        if (apn_table[i].mcc == mcc && apn_table[i].mnc == mnc) {
            return apn_table[i].apn;
        }
    }
    return "v-internet";  
}

bool gsm_nw_basic(){
    char line[50];
    switch (gsm_nw_ctx.basic.step)
    {
    case 0:
        send_debug(">>> check AT \r\n");
        send_at("AT\r\n");
        gsm_nw_ctx.time_stamp = get_tick_ms();
        gsm_nw_ctx.basic.step = 1;
        break;

    case 1:
        // Pop và skip URC spam khi đang chờ OK
        while(gsm_send_data_queue_pop(line, sizeof(line))){
            log_raw_line(line);
            if(at_parser_line(line,&urc)){
                if(urc.type == URC_OK){
                    gsm_nw_ctx.basic.step = 2;
                    return true;
                }
                else if(urc.type == URC_ERROR){
                    gsm_nw_handle_error();
                    return false;
                }
                // Skip các URC không liên quan
            }
            // Skip URC spam không parse được
        }
        if (is_timeout(gsm_nw_ctx.time_stamp,TIME_OUT)){
            send_debug("time out AT\r\n");
            gsm_nw_handle_error();
            return false;
        }
        break;
    // case 2:
    //     send_debug(">>> check sim ready \r\n");
    //     send_at("AT+CPIN?\r\n");
    //     gsm_nw_ctx.time_stamp = get_tick_ms();
    //     gsm_nw_ctx.basic.step = 3;
    //     break;
    // case 3:
    //     if(gsm_send_data_queue_pop(line, sizeof(line))){
    //         log_raw_line(line);
    //         if(at_parser_line(line,&urc)){
    //             if(urc.type == URC_CPIN_READY){
    //                 gsm_nw_ctx.basic.step = 4;
    //                 return true;
    //             }
    //             else if (urc.type == URC_CPIN_PIN || urc.type == URC_CPIN_PUK){
    //                 send_debug(">>> [sim error] no pin or no puk");
    //                 gsm_nw_handle_error();
    //                 return false;
    //             }
    //         }
    //     }
    //     if (is_timeout(gsm_nw_ctx.time_stamp,TIME_OUT)){
    //         send_debug("time out CPIN\r\n");
    //         gsm_nw_handle_error();
    //         return false;
    //     }
    //     break;
    case 2:
        send_debug(">>> check CREG \r\n");
        send_at("AT+CREG?\r\n");
        gsm_nw_ctx.time_stamp = get_tick_ms();
        gsm_nw_ctx.basic.step = 3;
        break;

    case 3:
        if(gsm_send_data_queue_pop(line, sizeof(line))){
            log_raw_line(line);
            if(at_parser_line(line,&urc)){
                if(urc.type == URC_CREG){
                    if (urc.v1 == 1 || urc.v1 == 5){
                        gsm_nw_ctx.basic.step = 4;
                        return true;    
                    }
                        else if (urc.v1 == 2 || urc.v1 == 3 || urc.v1 == 4) {
                        send_debug(">>> [sim] not registered / searching / denied / unknown\r\n");
                        gsm_nw_handle_error();
                        return false;
                    } 
                }
                else if(urc.type == URC_ERROR ){    
                    send_debug(">>> [sim error] error");
                    gsm_nw_handle_error();
                    return false;
                }
            }
        }
        if (is_timeout(gsm_nw_ctx.time_stamp,TIME_OUT)){
            send_debug("time out CREG\r\n");
            gsm_nw_handle_error();
            return false;
        }
        break;

    case 4:
        send_debug(">>> set SMS text mode\r\n");
        send_at("AT+CMGF=1\r\n");
        gsm_nw_ctx.time_stamp = get_tick_ms();
        gsm_nw_ctx.basic.step = 5;
        break;

    case 5:
        if(gsm_send_data_queue_pop(line, sizeof(line))){
            log_raw_line(line);
            if(at_parser_line(line,&urc)){
                if(urc.type == URC_OK){
                    gsm_nw_ctx.basic.step = 6;
                    return true;
                }
                else if(urc.type == URC_ERROR ){
                    send_debug(">>> [sim error] error");
                    gsm_nw_handle_error();
                }
            }
        }
        if (is_timeout(gsm_nw_ctx.time_stamp,TIME_OUT)){
            send_debug("time out CMGF\r\n");
            gsm_nw_handle_error();
            return false;
        }
        break; 
        
    case 6:
        send_debug(">>> set char set GSM 7-bit\r\n");
        send_at("AT+CSCS=\"GSM\"\r\n");
        gsm_nw_ctx.time_stamp = get_tick_ms();
        gsm_nw_ctx.basic.step = 7;
        break;

    case 7:
        // Pop và skip URC spam (+CGEV, *ATREADY, *ISIMAID, +CPIN: READY) khi đang chờ OK
        while(gsm_send_data_queue_pop(line, sizeof(line))){
            log_raw_line(line);
            if(at_parser_line(line,&urc)){
                if(urc.type == URC_OK){
                    gsm_nw_ctx.basic.step = 8;
                    return true;
                }
                else if(urc.type == URC_ERROR ){
                    send_debug(">>> [sim error] CSCS error\r\n");
                    gsm_nw_handle_error();
                    return false;
                }
                // Skip các URC không liên quan (CPIN, CREG, etc.)
                // Tiếp tục pop line tiếp theo
            } else {
                // Không parse được (URC spam như +CGEV, *ATREADY) → skip
                // Tiếp tục pop line tiếp theo
            }
        }
        // Tăng timeout lên 15s để chờ response (module có thể chậm)
        if (is_timeout(gsm_nw_ctx.time_stamp, 15000)){
           send_debug("time out CSCS\r\n");
            gsm_nw_handle_error();
            return false;
        }
        break;
        
    case 8:
        send_debug(">>> set storage SIM (SM)\r\n");
        send_at("AT+CPMS=\"SM\",\"SM\",\"SM\"\r\n");
        gsm_nw_ctx.time_stamp = get_tick_ms();
        gsm_nw_ctx.basic.step = 9;
        break;

    case 9:
        if(gsm_send_data_queue_pop(line, sizeof(line))){
            log_raw_line(line);
            if(at_parser_line(line,&urc)){
                if(urc.type == URC_OK){
                    gsm_nw_ctx.basic.step = 10;
                    return true;
                }
                else if(urc.type == URC_ERROR ){
                    send_debug(">>> [sim error] error");
                    gsm_nw_handle_error();
                }
            }
        }
        if (is_timeout(gsm_nw_ctx.time_stamp,TIME_OUT)){
            send_debug("time out AT+CPMS\r\n");
            gsm_nw_handle_error();
            return false;
        }
        break;

    case 10:
        send_debug(">>> check cimi for save when set apn\r\n");
        send_at("AT+CIMI\r\n");
        gsm_nw_ctx.time_stamp = get_tick_ms();
        gsm_nw_ctx.basic.step = 11;
        break;
        
    case 11:
        if (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
            if (at_parser_line(line, &urc)) {
                if (urc.type == URC_IMSI) {
                    
                    gsm_nw_ctx.lte.mcc = (int)urc.v1;
                    gsm_nw_ctx.lte.mnc = (int)urc.v2;
                    const char *apn = apn_from_mcc_mnc(gsm_nw_ctx.lte.mcc,
                                                       gsm_nw_ctx.lte.mnc);
                    strncpy(gsm_nw_ctx.lte.apn, apn, sizeof(gsm_nw_ctx.lte.apn) - 1);
                    gsm_nw_ctx.lte.apn[sizeof(gsm_nw_ctx.lte.apn) - 1] = '\0';

                    gsm_nw_ctx.basic.step = 12;
                    return true;
                } else if (urc.type == URC_ERROR) {
                    send_debug(">>> [sim error] error");
                    gsm_nw_handle_error();
                    return false;
                }
            }
        }
        if (is_timeout(gsm_nw_ctx.time_stamp, TIME_OUT)) {
            send_debug("time out CIMI\r\n");
            gsm_nw_handle_error();
            return false;
        }
        break;

    case 12:
        send_debug(">>> turn on return error\r\n");
        send_at("AT+CMEE=1\r\n");
        gsm_nw_ctx.time_stamp = get_tick_ms();
        gsm_nw_ctx.basic.step = 13;
        break;

    case 13:
        if (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
            if (at_parser_line(line, &urc)) {
                if (urc.type == URC_OK) {
                    if (gsm_nw_ctx.lte.need_data) {
                        gsm_nw_ctx.state = GSM_NW_LTE;
                        gsm_nw_ctx.lte.step = 0;
                        return true;
                    } else {
                        gsm_nw_ctx.state = GSM_NW_DONE;
                        gsm_nw_ctx.basic.step = 0;
                        return true;
                    }
                } else if (urc.type == URC_ERROR) {
                    send_debug(">>> [sim error] error");
                    gsm_nw_handle_error();
                    return false;
                }
            }
        }
        if (is_timeout(gsm_nw_ctx.time_stamp, TIME_OUT)) {
            send_debug("time out CMEE\r\n");
            gsm_nw_handle_error();
            return false;
        }
        break;
    
    default:
        break;
    }
    return false;
}

bool gsm_nw_lte(void){
    char line[50];
    switch (gsm_nw_ctx.lte.step)
    {
    case 0:
        send_debug(">>> turn on gprs\r\n");
        send_at("AT+CGATT=1\r\n");
        gsm_nw_ctx.time_stamp = get_tick_ms();
        gsm_nw_ctx.lte.step = 1;
        break;

    case 1:
        if(gsm_send_data_queue_pop(line, sizeof(line))){
            log_raw_line(line);
            if(at_parser_line(line,&urc)){
                if(urc.type == URC_OK){
                    gsm_nw_ctx.lte.step = 2;
                    return true;
                }
                else if(urc.type == URC_ERROR ){
                    send_debug(">>> [sim error] error");
                    gsm_nw_handle_error();
                }
            }
        }
        if (is_timeout(gsm_nw_ctx.time_stamp,TIME_OUT)){
            send_debug("time out GPRS\r\n");
            gsm_nw_handle_error();
            return false;
        }
        break;

    case 2: {
        char cmd[64];
        const char *apn = gsm_nw_ctx.lte.apn[0] ? gsm_nw_ctx.lte.apn : "v-internet";

        snprintf(cmd, sizeof(cmd),
                 "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", apn);
        send_at(cmd);
        gsm_nw_ctx.time_stamp = get_tick_ms();
        gsm_nw_ctx.lte.step = 3;
        break;
    }

    case 3:
        if(gsm_send_data_queue_pop(line, sizeof(line))){
            log_raw_line(line);
            if(at_parser_line(line,&urc)){
                if(urc.type == URC_OK){
                    gsm_nw_ctx.lte.step = 4;                    
                    return true;
                }
                else if(urc.type == URC_ERROR ){
                    send_debug(">>> [sim error] error");
                    gsm_nw_handle_error();
                }
            }
        }
        if (is_timeout(gsm_nw_ctx.time_stamp,TIME_OUT)){
            send_debug("time out APN\r\n");
            gsm_nw_handle_error();
            return false;
        }
        break;
    
    case 4: {
        send_debug(">>> check ip \r\n");
        send_at("AT+CGPADDR=1\r\n");
        gsm_nw_ctx.time_stamp = get_tick_ms();
        gsm_nw_ctx.lte.step = 5;
        break;
    }

    case 5:
        if(gsm_send_data_queue_pop(line, sizeof(line))){
            log_raw_line(line);
            if(at_parser_line(line,&urc)){
                if(urc.type == URC_CGPADDR){
                    if(urc.text[0] != '\0'){
                        send_debug(">>> IP: ");
                        send_debug(urc.text);
                        send_debug("\r\n");
                        gsm_nw_ctx.state = GSM_NW_DONE;           
                        return true;
                    } else {
                        send_debug(">>> No IP yet, may need activate PDP\r\n");
                        gsm_nw_ctx.state = GSM_NW_DONE;
                    }
                }
                else if(urc.type == URC_OK){
                    send_debug(">>> OK but no IP, may need activate PDP\r\n");
                }
                else if(urc.type == URC_ERROR ){
                    send_debug(">>> [sim error] error");
                    gsm_nw_handle_error();
                    return false;
                }
            }
        }
        if (is_timeout(gsm_nw_ctx.time_stamp,6000)){
            send_debug("time out check IP\r\n");
            gsm_nw_handle_error();
            return false;
        }
        break;
    default:
        break;
    }
    return false;
}

bool gsm_nw_reset_sim(void) {
    char line[50];
    
    switch (gsm_nw_ctx.lte.step) {
    case 0:
        send_debug(">>> Reset SIM: AT+CFUN=1,1\r\n");
        send_at("AT+CFUN=1,1\r\n");
        
        gsm_nw_ctx.time_stamp = get_tick_ms();
        gsm_nw_ctx.lte.step = 1;
        break;
        
    case 1:
        if (gsm_send_data_queue_pop(line, sizeof(line))) {
            log_raw_line(line);
            if (at_parser_line(line, &urc)) {
                if (urc.type == URC_OK) {
                    send_debug(">>> SIM reset OK, restart init\r\n");
                    delay_ms(5000);
                    gsm_nw_ctx.lte.step = 0;
                    gsm_nw_ctx.state = GSM_NW_BASIC;

                    gsm_nw_ctx.basic.step = 0;
                    gsm_nw_ctx.retry_count = 0;
                    return true;
                } else if (urc.type == URC_ERROR) {
                    send_debug(">>> SIM reset failed\r\n");
                    gsm_nw_ctx.lte.step = 0;
                    gsm_nw_handle_error();  
                    return false;
                }
            }
        }
        if (is_timeout(gsm_nw_ctx.time_stamp, 10000)) {
        	send_debug("time out reset SIM\r\n");
            gsm_nw_ctx.lte.step = 0;
            gsm_nw_handle_error();
            return false;
        }
        break;
        
    default:
        gsm_nw_ctx.lte.step = 0;
        break;
    }
    return false;
}

void gsm_nw_process(void){
    switch (gsm_nw_ctx.state)
    {
    case GSM_NW_BASIC:
        gsm_nw_basic();
        break;
    case GSM_NW_LTE:
        gsm_nw_lte();
        break;
    case GSM_NW_DONE:
        
        break;
    case GSM_NW_RESET_SIM:
        gsm_nw_reset_sim();
        break;
    default:
        break;
    }
}
