/**
  ******************************************************************************
  * @file    bm_rcc.h
  * @brief   Modulo de relojes locales del Cortex-M4 para 3_DSP_Ethernet.
  *
  *  Invariante estricta:
  *    El M4 solo escribe en registros RCC_C2->*ENR (sus propios registros).
  *    Grep obligatorio: grep -rn 'RCC[-]>' CM4_App/ debe salir vacio.
  ******************************************************************************
  */

#ifndef BM_RCC_H
#define BM_RCC_H

#include <stdint.h>
#include "stm32h7xx.h"
#include "dsp_link.h"

/**
  * @brief  Espera a que el CM7 monte los relojes globales y firme DSP_LINK_MAGIC.
  * @return Puntero al contrato DSP_LINK validado.
  */
const DSP_Link_t *BM_RCC_WaitCm7(void);

/**
  * @brief  Inicializa SysTick del CM4 a 1 ms usando la frecuencia del contrato.
  * @param  hclk_hz  Frecuencia de HCLK (típicamente 200 MHz).
  */
void BM_RCC_SysTickInit(uint32_t hclk_hz);

/**
  * @brief  Retorna los milisegundos transcurridos desde el arranque del M4.
  */
uint32_t BM_GetTick(void);

/**
  * @brief  Espera bloqueante en milisegundos usando SysTick.
  */
void BM_Delay(uint32_t ms);

#endif /* BM_RCC_H */
