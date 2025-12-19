#ifndef __GSM_SEND_DATA_QUEUE_H__
#define __GSM_SEND_DATA_QUEUE_H__
/*
    include
*/
#include <stdint.h>
#include <stdbool.h>


/*
    struct
*/
#define GSM_URC_QUEUE_SIZE 10 
#define GSM_URC_LINE_MAX_LEN 128 

typedef struct {
    char lines[GSM_URC_QUEUE_SIZE][GSM_URC_LINE_MAX_LEN];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    bool push_done;
} gsm_urc_queue_t;

void gsm_send_data_queue_init(void);
bool gsm_send_data_queue_pop(char *line, uint16_t max_len);
void gsm_send_data_queue_proces(void);
bool gsm_get_line_queue(char *line, uint16_t max_len);
void delete_line(char *line);

#endif //__GSM_SEND_DATA_QUEUE_H__