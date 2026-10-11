#ifndef __SD_H
#define __SD_H

#include "stm32f1xx_hal.h"

#define SD_CS_PIN       GPIO_PIN_4
#define SD_CS_PORT      GPIOA

#define SD_CS_LOW() 		HAL_GPIO_WritePin(SD_CS_PORT,SD_CS_PIN,GPIO_PIN_RESET)
#define SD_CS_HIGH()    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET)

uint8_t SD_CARD_Init(void);
uint8_t SD_ReadSector(uint8_t *pbuf, uint32_t LBA, uint8_t cnt);
uint8_t SD_WriteSector(const uint8_t *pbuf, uint32_t LBA, uint8_t cnt);

#endif
