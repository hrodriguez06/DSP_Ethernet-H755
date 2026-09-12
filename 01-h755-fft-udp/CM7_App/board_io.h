/**
  ******************************************************************************
  * @file    board_io.h
  * @brief   LEDs y boton de usuario de la NUCLEO-H755ZI-Q para CM7.
  ******************************************************************************
  */
#ifndef BOARD_IO_H
#define BOARD_IO_H

#include "stm32h7xx_hal.h"

#define LD1_Pin              GPIO_PIN_0
#define LD1_GPIO_Port        GPIOB

#define LD2_Pin              GPIO_PIN_1
#define LD2_GPIO_Port        GPIOE

#define LD3_Pin              GPIO_PIN_14
#define LD3_GPIO_Port        GPIOB

#define B1_Pin               GPIO_PIN_13
#define B1_GPIO_Port         GPIOC
#define B1_PRESSED_STATE     GPIO_PIN_SET

void Board_IO_Init(void);
void Board_IO_SetLed(uint8_t led_idx, uint8_t state);
void Board_IO_ToggleLed(uint8_t led_idx);

#endif /* BOARD_IO_H */
