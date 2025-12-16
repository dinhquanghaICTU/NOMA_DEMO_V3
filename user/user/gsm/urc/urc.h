#ifndef __URC_H__
#define __URC_H__

#include "stdint.h"
#include "stdbool.h"
#include <stdlib.h>
#include <ctype.h>

typedef enum {
    URC_OK,
    URC_ERROR,
    
    URC_CPIN_READY,
    URC_CPIN_PIN,
    URC_CPIN_PUK,
    
    URC_CREG,

    URC_IMSI,

    URC_CGPADDR,

    URC_CMGS_PROMPT,   
    URC_CMGS,          
    URC_CMS_ERROR,
    
    URC_CMTI,          // +CMTI: "SM",1 (SMS mới đến)
    URC_SMS_TEXT,
    URC_CMGR,

    URC_UNKNOWN,
} urc_type_t;

typedef struct {
    urc_type_t type;
    uint32_t v1;
    uint32_t v2;
    uint32_t v3;
    char text[64];  
} urc_t;

bool at_parser_line(const char *line, urc_t *out);


#endif // __URC_H__