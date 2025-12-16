#include "urc.h"
#include <string.h>
#include <stdio.h>
 
bool at_parser_line(const char *line, urc_t *out){
    if (!line || !out) return false;
    memset(out, 0, sizeof(*out));
    out->type = URC_UNKNOWN;

    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n')) {
        n--;
    }
    if (n == 0) return false;

    if ((n >= 2 && line[0] == 'A' && line[1] == 'T') ||
        (n >= 3 && strncmp(line, "AT+", 3) == 0)) {
        return false;
    }

  
    if (n == 2 && line[0] == 'O' && line[1] == 'K') {
        out->type = URC_OK;
        return true;
    }
    if (n == 5 && strncmp(line, "ERROR", 5) == 0) {
        out->type = URC_ERROR;
        return true;
    }

    if (n >= 6 && strncmp(line, "+CPIN:", 6) == 0) {
        const char *p = line + 6;
        while (*p == ' ' || *p == '\t') p++;

        if (strstr(p, "READY")) {
            out->type = URC_CPIN_READY;   // cần định nghĩa trong urc_t
        } else if (strstr(p, "SIM PIN")) {
            out->type = URC_CPIN_PIN;     // SIM cần PIN
        } else if (strstr(p, "SIM PUK")) {
            out->type = URC_CPIN_PUK;     // SIM cần PUK
        } else {
            out->type = URC_UNKNOWN;
        }
        return true;
    }

     // +CREG:
    if (n >= 6 && strncmp(line, "+CREG:", 6) == 0) {
        int reg_n = -1, stat = -1;
        const char *p = line + 6;
        if (sscanf(p, "%d,%d", &reg_n, &stat) == 2) {
            out->type = URC_CREG;
            out->v1 = stat;   
            return true;
        }
    }


    {
        bool all_digit = true;
        for (size_t i = 0; i < n; i++) {
            if (line[i] < '0' || line[i] > '9') {
                all_digit = false;
                break;
            }
        }
        if (all_digit && n >= 5) {
            out->type = URC_IMSI;

            // lưu MCC/MNC vào v1/v2
            int mcc = (line[0]-'0')*100 + (line[1]-'0')*10 + (line[2]-'0');
            int mnc = (line[3]-'0')*10  + (line[4]-'0');        // giả sử MNC 2 số

            out->v1 = mcc;
            out->v2 = mnc;

            // nếu urc_t có field string, có thể lưu luôn IMSI:
            // snprintf(out->str, sizeof(out->str), "%.*s", (int)n, line);

            return true;
        }
    }

    if (n >= 9 && strncmp(line, "+CGPADDR:", 9) == 0) {
    const char *p = line + 9;
    int cid = -1;
    char ip[16] = {0};
    
    
    if (sscanf(p, "%d,\"%15[^\"]\"", &cid, ip) == 2) {
        out->type = URC_CGPADDR;
        out->v1 = cid;   
        strncpy(out->text, ip, sizeof(out->text) - 1); 
        out->text[sizeof(out->text) - 1] = '\0';
        return true;
    }
    
    else if (sscanf(p, "%d", &cid) == 1) {
        out->type = URC_CGPADDR;
        out->v1 = cid;
        out->text[0] = '\0';  
        return true;
    }
}

    // > (prompt cho AT+CMGS)
    if (n == 1 && line[0] == '>') {
        out->type = URC_CMGS_PROMPT;
        return true;
    }

    // +CMGS: <ref>
    if (n >= 6 && strncmp(line, "+CMGS:", 6) == 0) {
        int ref = -1;
        const char *p = line + 6;
        if (sscanf(p, " %d", &ref) == 1) {
            out->type = URC_CMGS;
            out->v1 = ref;
            return true;
        }
    }

    // +CMS ERROR: <code>
    if (n >= 11 && strncmp(line, "+CMS ERROR:", 11) == 0) {
        int code = -1;
        const char *p = line + 11;
        if (sscanf(p, " %d", &code) == 1) {
            out->type = URC_CMS_ERROR;
            out->v1 = code;
            return true;
        }
    }


    if (n >= 6 && strncmp(line, "+CMTI:", 6) == 0) {
        char storage[8] = {0};
        int index = -1;
        const char *p = line + 6;
        
        if (sscanf(p, " \"%7[^\"]\",%d", storage, &index) == 2) {
            out->type = URC_CMTI;
            out->v1 = index;  
            strncpy(out->text, storage, sizeof(out->text) - 1);  
            out->text[sizeof(out->text) - 1] = '\0';
            return true;
        }
    }

    // +CMGR: "REC UNREAD","+84xxxx","",...
    if (n >= 6 && strncmp(line, "+CMGR:", 6) == 0) {
        const char *p = line + 6;
        char status[16] = {0};
        char phone[24]  = {0};
        if (sscanf(p, " \"%15[^\"]\",\"%23[^\"]\"", status, phone) == 2) {
            out->type = URC_CMGR;
            strncpy(out->text, phone, sizeof(out->text) - 1);
            out->text[sizeof(out->text) - 1] = '\0';
            return true;
        } else {
            out->type = URC_CMGR;
            return true; 
        }
    }

    // Dòng nội dung SMS (chỉ toàn ký tự in được, không bắt đầu bằng '+')
    {
        bool all_printable = true;
        for (size_t i = 0; i < n; i++) {
            if ((unsigned char)line[i] < 0x20 && line[i] != '\t') {
                all_printable = false; break;
            }
        }
        if (all_printable && line[0] != '+') {
            out->type = URC_SMS_TEXT;
            snprintf(out->text, sizeof(out->text), "%.*s", (int)n, line);
            return true;
        }
    }

    return false;
}