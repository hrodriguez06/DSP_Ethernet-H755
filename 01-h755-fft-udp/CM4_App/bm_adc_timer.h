/**
  ******************************************************************************
  * @file    bm_adc_timer.h
  * @brief   Adquisicion periodica determinista en Cortex-M4: ADC1 disparado
  *          por TIM3 TRGO en modo Ping-Pong hacia SRAM4.
  ******************************************************************************
  */

#ifndef BM_ADC_TIMER_H
#define BM_ADC_TIMER_H

#include <stdint.h>
#include "stm32h7xx.h"
#include "dsp_link.h"

/**
  * @brief  Inicializa GPIO (PA3 analogo), ADC1 (16-bit) y TIM3 (TRGO @ fs).
  * @param  timer_clk_hz  Frecuencia de reloj del temporizador (típicamente 200 MHz).
  * @param  fs_hz         Frecuencia de muestreo deseada (ej. 25600 o 25000 Hz).
  * @return 0 si OK, negativo si error de calibracion/hardware.
  */
int BM_ADC_Timer_Init(uint32_t timer_clk_hz, uint32_t fs_hz);

/**
  * @brief  Inicia el muestreo periodico continuo.
  */
void BM_ADC_Timer_Start(void);

/**
  * @brief  Detiene el muestreo periodico.
  */
void BM_ADC_Timer_Stop(void);

/**
  * @brief  Reconfigura la frecuencia de muestreo en tiempo de ejecucion.
  */
void BM_ADC_Timer_SetFs(uint32_t timer_clk_hz, uint32_t fs_hz);

#endif /* BM_ADC_TIMER_H */
