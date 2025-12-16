#ifndef __SPI_H__
#define __SPI_H__

/*
    include 
*/
#include "stm32f10x_spi.h"
#include "stm32f10x_gpio.h"

/*
    define
*/
#define SPI_FLASH_CS_Pin    GPIO_Pin_4
#define SPI_FLASH_CS_GPIO   GPIOA
#define SPI_FLASH_CS_LOW()  GPIO_ResetBits(SPI_FLASH_CS_GPIO, SPI_FLASH_CS_Pin)
#define SPI_FLASH_CS_HIGH() GPIO_SetBits(SPI_FLASH_CS_GPIO, SPI_FLASH_CS_Pin)

/*
    funcion
*/

void spi_init(void);
uint8_t spi_transfer(uint8_t data);

#endif //__SPI_H__