#include "w25Qx.h"
#include "hardware.h"


static void w25qxx_send_cmd(uint8_t cmd) {
    SPI_FLASH_CS_LOW();
    spi_transfer(cmd);
    SPI_FLASH_CS_HIGH();
}

void w25qxx_init(void) {
    spi_init();
    SPI_FLASH_CS_HIGH();
    w25qxx_read_id(); 
}

uint32_t w25qxx_read_id(void) {
    uint32_t id = 0;
    SPI_FLASH_CS_LOW();
    spi_transfer(W25Q_CMD_READ_ID);
    id = ((uint32_t)spi_transfer(0xFF) << 16);
    id |= ((uint32_t)spi_transfer(0xFF) << 8);
    id |= (uint32_t)spi_transfer(0xFF);
    SPI_FLASH_CS_HIGH();
    return id;
}

void w25qxx_write_enable(void) {
    w25qxx_send_cmd(W25Q_CMD_WRITE_ENABLE);
}

void w25qxx_wait_busy(void) {
    uint8_t status;
    do {
        SPI_FLASH_CS_LOW();
        spi_transfer(W25Q_CMD_READ_STATUS);
        status = spi_transfer(0xFF);
        SPI_FLASH_CS_HIGH();
    } while (status & 0x01);
}

void w25qxx_erase_sector(uint32_t addr) {
    w25qxx_write_enable();
    SPI_FLASH_CS_LOW();
    spi_transfer(W25Q_CMD_SECTOR_ERASE);
    spi_transfer((addr >> 16) & 0xFF);
    spi_transfer((addr >> 8) & 0xFF);
    spi_transfer(addr & 0xFF);
    SPI_FLASH_CS_HIGH();
    w25qxx_wait_busy();
}

void w25qxx_write_page(uint32_t addr, const uint8_t *data, uint16_t len) {
    if (len > W25QXX_PAGE_SIZE) len = W25QXX_PAGE_SIZE;

    w25qxx_write_enable();
    SPI_FLASH_CS_LOW();
    spi_transfer(W25Q_CMD_PAGE_PROGRAM);
    spi_transfer((addr >> 16) & 0xFF);
    spi_transfer((addr >> 8) & 0xFF);
    spi_transfer(addr & 0xFF);
    for (uint16_t i = 0; i < len; i++) {
        spi_transfer(data[i]);
    }
    SPI_FLASH_CS_HIGH();
    w25qxx_wait_busy();
}


void w25qxx_read(uint32_t addr, uint8_t *data, uint16_t len) {
    SPI_FLASH_CS_LOW();
    spi_transfer(W25Q_CMD_READ_DATA);
    spi_transfer((addr >> 16) & 0xFF);
    spi_transfer((addr >> 8) & 0xFF);
    spi_transfer(addr & 0xFF);
    for (uint16_t i = 0; i < len; i++) {
        data[i] = spi_transfer(0xFF);
    }
    SPI_FLASH_CS_HIGH();
}

