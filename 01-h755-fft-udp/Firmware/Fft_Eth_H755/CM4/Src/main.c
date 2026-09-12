/**
  ******************************************************************************
  * @file    main.c
  * @brief   Punto de entrada del Cortex-M4 en 01-h755-fft-udp.
  *
  *  El Cortex-M4 es el motor de tiempo real duro:
  *    1. Espera el apreton de manos de arranque (DSP_LINK_MAGIC).
  *    2. Configura SysTick y perifericos con sus registros RCC_C2.
  *    3. Inicia muestreo periodico ADC1 disparado por TIM3 TRGO a fs fija.
  *    4. Duerme con WFI entre muestras, garantizando minimo ruido en masa.
  ******************************************************************************
  */

#include "stm32h7xx.h"
#include "dsp_link.h"
#include "bm_rcc.h"
#include "bm_adc_timer.h"

int main(void)
{
  /* 1. Esperar a que el Cortex-M7 configure los relojes y escriba el contrato */
  const DSP_Link_t *link = BM_RCC_WaitCm7();
  if (link == (const DSP_Link_t *)0)
  {
    for (;;) { __WFI(); } /* Error: CM7 nunca firmo el contrato */
  }

  /* 2. Inicializar SysTick local del CM4 */
  BM_RCC_SysTickInit(link->hclk_hz);

  /* 3. Inicializar indices del buffer Ping-Pong */
  DSP_LINK->active_idx    = 0u;
  DSP_LINK->ready_idx     = 0u;
  DSP_LINK->sample_count  = 0u;
  DSP_LINK->block_id      = 0u;
  DSP_LINK->overrun_count = 0u;
  DSP_LINK->flags         = 0u;
  DSP_LINK->cmd_ack       = DSP_LINK->cmd_seq;
  __DSB();

  /* 4. Inicializar ADC1 y TIM3 */
  uint32_t tim_clk = (link->apb1_timer_hz != 0u) ? link->apb1_timer_hz : 200000000u;
  uint32_t fs      = (link->fs_hz != 0u) ? link->fs_hz : DSP_FS_DEFAULT;

  if (BM_ADC_Timer_Init(tim_clk, fs) != 0)
  {
    for (;;) { __WFI(); } /* Error de calibracion o encendido del ADC */
  }

  /* 5. Iniciar adquisicion continua */
  BM_ADC_Timer_Start();

  /* 6. Lazo principal de baja potencia y atencion de comandos */
  for (;;)
  {
    /* Atender comandos entrantes desde el CM7 */
    if (DSP_LINK->cmd_seq != DSP_LINK->cmd_ack)
    {
      if (DSP_LINK->cmd_run == 0u && (DSP_LINK->flags & DSP_FLAG_RUNNING))
      {
        BM_ADC_Timer_Stop();
      }
      else if (DSP_LINK->cmd_run != 0u && !(DSP_LINK->flags & DSP_FLAG_RUNNING))
      {
        BM_ADC_Timer_Start();
      }

      if (DSP_LINK->cmd_fs_hz != 0u && DSP_LINK->cmd_fs_hz != DSP_LINK->fs_hz)
      {
        int was_running = (DSP_LINK->flags & DSP_FLAG_RUNNING) ? 1 : 0;
        BM_ADC_Timer_Stop();
        DSP_LINK->fs_hz = DSP_LINK->cmd_fs_hz;
        BM_ADC_Timer_SetFs(tim_clk, DSP_LINK->fs_hz);
        if (was_running)
        {
          BM_ADC_Timer_Start();
        }
      }

      DSP_LINK->cmd_ack = DSP_LINK->cmd_seq;
      __DSB();
    }

    /* Dormir hasta la siguiente interrupcion (SysTick o ADC_IRQHandler).
       WFI apaga el reloj del core sin detener timers ni ADC, eliminando
       el ruido de conmutacion digital durante el muestreo. */
    __WFI();
  }
}
