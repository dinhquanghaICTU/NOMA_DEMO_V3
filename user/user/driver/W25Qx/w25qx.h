
#ifndef __W25QXX_H__
#define __W25QXX_H__
/*
    include
*/
#include <stdint.h>
#include "spi/spi.h"
/* 
    define
*/
#define W25QXX_PAGE_SIZE    256
#define W25QXX_SECTOR_SIZE  4096
#define W25QXX_TOTAL_SIZE   (8 * 1024 * 1024) // W25Q64 = 8MB


#define W25Q_CMD_WRITE_ENABLE    0x06
#define W25Q_CMD_READ_STATUS     0x05
#define W25Q_CMD_READ_ID         0x9F
#define W25Q_CMD_READ_DATA       0x03
#define W25Q_CMD_PAGE_PROGRAM    0x02
#define W25Q_CMD_SECTOR_ERASE    0x20

/*
    funcion
*/
void w25qxx_init(void);
uint32_t w25qxx_read_id(void);
void w25qxx_write_enable(void);
void w25qxx_wait_busy(void);
void w25qxx_erase_sector(uint32_t addr);          
void w25qxx_write_page(uint32_t addr, const uint8_t *data, uint16_t len); 
void w25qxx_write(uint32_t addr, const uint8_t *data, uint16_t len);     
void w25qxx_read(uint32_t addr, uint8_t *data, uint16_t len);

#endif // __W25QXX_H__

