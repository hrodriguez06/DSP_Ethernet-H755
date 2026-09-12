/**
  ******************************************************************************
  * @file    vcp_uart.c
  * @brief   USART3 hacia el VCP del ST-LINK, y redireccion de printf.
  ******************************************************************************
  */

#include "vcp_uart.h"
#include <stdio.h>

UART_HandleTypeDef huart3;

int VCP_UART_Init(void)
{
  __HAL_RCC_USART3_CLK_ENABLE();

  huart3.Instance                    = USART3;
  huart3.Init.BaudRate               = 115200;
  huart3.Init.WordLength             = UART_WORDLENGTH_8B;
  huart3.Init.StopBits               = UART_STOPBITS_1;
  huart3.Init.Parity                 = UART_PARITY_NONE;
  huart3.Init.Mode                   = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling           = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    return -1;
  }

  setvbuf(stdout, NULL, _IONBF, 0);
  return 0;
}

int __io_putchar(int ch)
{
  uint8_t c = (uint8_t)ch;
  HAL_UART_Transmit(&huart3, &c, 1, HAL_MAX_DELAY);
  return ch;
}

int _write(int file, char *ptr, int len)
{
  (void)file;
  if (len > 0)
  {
    HAL_UART_Transmit(&huart3, (uint8_t *)ptr, (uint16_t)len, HAL_MAX_DELAY);
  }
  return len;
}
