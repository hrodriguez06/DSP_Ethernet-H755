/**
  ******************************************************************************
  * @file    board_io.c
  * @brief   LEDs y boton de usuario de la NUCLEO-H755ZI-Q para CM7.
  ******************************************************************************
  */

#include "board_io.h"

void Board_IO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOB, LD1_Pin | LD3_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOE, LD2_Pin, GPIO_PIN_RESET);

  /* LD1 y LD3 en GPIOB */
  GPIO_InitStruct.Pin   = LD1_Pin | LD3_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* LD2 en GPIOE */
  GPIO_InitStruct.Pin   = LD2_Pin;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /* B1 en GPIOC */
  GPIO_InitStruct.Pin   = B1_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

void Board_IO_SetLed(uint8_t led_idx, uint8_t state)
{
  GPIO_PinState st = state ? GPIO_PIN_SET : GPIO_PIN_RESET;
  if (led_idx == 1) HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, st);
  else if (led_idx == 2) HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, st);
  else if (led_idx == 3) HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, st);
}

void Board_IO_ToggleLed(uint8_t led_idx)
{
  if (led_idx == 1) HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);
  else if (led_idx == 2) HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
  else if (led_idx == 3) HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
}
