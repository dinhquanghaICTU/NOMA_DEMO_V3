#include "spi.h"

void spi_init(void){
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
    /*
        confic gpio_spi 
    */
    GPIO_InitTypeDef gpio_config_spi;
    // configg NSS -> pa4
    gpio_config_spi.GPIO_Pin = GPIO_Pin_4;
    gpio_config_spi.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio_config_spi.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio_config_spi);
    //config sck -> pa5
    gpio_config_spi.GPIO_Pin = GPIO_Pin_5;
    gpio_config_spi.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio_config_spi.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio_config_spi);
    //config MISO -> pa6
    gpio_config_spi.GPIO_Pin = GPIO_Pin_6;
    gpio_config_spi.GPIO_Mode= GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio_config_spi);
    //config MOSI -> pa7
    gpio_config_spi.GPIO_Pin =GPIO_Pin_7;
    gpio_config_spi.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio_config_spi.GPIO_Speed =GPIO_Speed_50MHz;  
    GPIO_Init(GPIOA, &gpio_config_spi);
    /*
        config for spi1
    */
    SPI_InitTypeDef config_spi;
    config_spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
    config_spi.SPI_Mode = SPI_Mode_Master;
    config_spi.SPI_CPHA= SPI_CPHA_2Edge; 
    config_spi.SPI_CPOL = SPI_CPOL_High;
    config_spi.SPI_NSS = SPI_NSS_Soft;
    config_spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    config_spi.SPI_DataSize = SPI_DataSize_8b;
    config_spi.SPI_FirstBit = SPI_FirstBit_MSB;
    config_spi.SPI_CRCPolynomial = 7;
    SPI_Init(SPI1, &config_spi);
    SPI_Cmd(SPI1, ENABLE);
    GPIO_SetBits(GPIOA, GPIO_Pin_4);
}

/*
    func send data
*/

uint8_t spi_transfer(uint8_t data) {
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(SPI1, data);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET);
    return SPI_I2S_ReceiveData(SPI1);
}