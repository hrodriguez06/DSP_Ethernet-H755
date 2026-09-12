/**
  ******************************************************************************
  * @file    vcp_uart.h
  * @brief   USART3 hacia el puerto COM virtual del ST-LINK (115200 8N1).
  ******************************************************************************
  */
#ifndef VCP_UART_H
#define VCP_UART_H

#include "stm32h7xx_hal.h"

extern UART_HandleTypeDef huart3;

int VCP_UART_Init(void);

#endif /* VCP_UART_H */
