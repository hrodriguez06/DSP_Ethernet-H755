/**
  ******************************************************************************
  * @file    bm_rcc.c
  * @brief   Espera de arranque coordinado y SysTick para Cortex-M4.
  ******************************************************************************
  */

#include "bm_rcc.h"

#define WAIT_LOOPS    20000000u

static volatile uint32_t s_tick_ms = 0;

void SysTick_Handler(void)
{
  s_tick_ms++;
}

uint32_t BM_GetTick(void)
{
  return s_tick_ms;
}

void BM_Delay(uint32_t ms)
{
  uint32_t t0 = s_tick_ms;
  while ((s_tick_ms - t0) < ms)
  {
    __NOP();
  }
}

const DSP_Link_t *BM_RCC_WaitCm7(void)
{
  uint32_t timeout = WAIT_LOOPS;

  /* El CM4 no tiene D-Cache, lee directamente de SRAM4 en cada iteración */
  while (DSP_LINK->magic != DSP_LINK_MAGIC)
  {
    if (--timeout == 0u)
    {
      return (const DSP_Link_t *)0; /* Error de timeout: CM7 no arrancó */
    }
  }

  __DSB();
  return (const DSP_Link_t *)DSP_LINK;
}

void BM_RCC_SysTickInit(uint32_t hclk_hz)
{
  /* Configurar SysTick para interrumpir cada 1 ms */
  if (hclk_hz < 1000000u)
  {
    hclk_hz = 200000000u; /* Fallback razonable si viniera corrupto */
  }

  SysTick->LOAD = (hclk_hz / 1000u) - 1u;
  SysTick->VAL  = 0u;
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                  SysTick_CTRL_TICKINT_Msk   |
                  SysTick_CTRL_ENABLE_Msk;
}
