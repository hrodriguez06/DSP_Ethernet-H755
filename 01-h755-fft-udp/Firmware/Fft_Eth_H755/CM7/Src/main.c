/**
  ******************************************************************************
  * @file    main.c
  * @brief   Punto de entrada del Cortex-M7 en 01-h755-fft-udp.
  *
  *  Responsabilidades del Cortex-M7:
  *    1. MPU: configurar SRAM3 (Ethernet) y SRAM4 (DSP_LINK) como no cacheables.
  *    2. Caches L1 I/D activadas.
  *    3. Arbol de relojes: 400 MHz con FLASH_LATENCY_4.
  *    4. Inicializacion de pines RMII (GPIOA, GPIOB, GPIOC, GPIOG) y USART3.
  *    5. Escribir contrato en SRAM4 y soltar al CM4 mediante DSP_LINK_MAGIC.
  *    6. Inicializar motor CMSIS-DSP (Hanning, RFFT 1024 puntos).
  *    7. Inicializar lwIP, supervisar enlace/DHCP y emitir telemetria UDP :5002.
  ******************************************************************************
  */

#include "main.h"
#include "lwip.h"
#include "dsp_link.h"
#include "dsp_engine.h"
#include "dsp_wavelet.h"
#include "udp_dsp.h"
#include "mpu_config.h"
#include "vcp_uart.h"
#include "board_io.h"
#include "app_ethernet.h"
#include <stdio.h>

extern ETH_HandleTypeDef heth;

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);

int main(void)
{
  /* 1. MPU antes de habilitar el D-Cache (SRAM3 y SRAM4 no cacheables) */
  APP_MPU_Config();
  SCB_EnableICache();
  SCB_EnableDCache();

  /* 2. Inicializacion basica del HAL */
  HAL_Init();

  /* 3. Arbol de relojes a 400 MHz con FLASH_LATENCY_4 */
  SystemClock_Config();

  /* 4. Inicializacion de GPIOs y perifericos de consola */
  MX_GPIO_Init();
  VCP_UART_Init();
  Board_IO_Init();

  printf("\r\n\r\n");
  printf("=====================================================\r\n");
  printf("  3_DSP_Ethernet / 01-h755-fft-udp (Cortex-M7)\r\n");
  printf("  Adquisicion determinista M4 + CMSIS-DSP + lwIP UDP\r\n");
  printf("=====================================================\r\n");

  /* 5. Configurar reloj de kernel del ADC: per_ck = HSI (64 MHz), ADCSEL = per_ck */
  MODIFY_REG(RCC->D1CCIPR, RCC_D1CCIPR_CKPERSEL, 0u);
  MODIFY_REG(RCC->D3CCIPR, RCC_D3CCIPR_ADCSEL, (0x2UL << RCC_D3CCIPR_ADCSEL_Pos));

  /* 6. Escribir el contrato de arranque en SRAM4 (0x38000000) */
  printf("[IPC] Escribiendo contrato en SRAM4 (0x%08lX)...\r\n", DSP_LINK_BASE);

  DSP_LINK->version        = DSP_LINK_VERSION;
  DSP_LINK->vrefint_cal    = *DSP_VREFINT_CAL_ADDR;
  DSP_LINK->sysclk_hz      = 400000000u;
  DSP_LINK->hclk_hz        = 200000000u;
  DSP_LINK->apb1_timer_hz  = 200000000u;
  DSP_LINK->fs_hz          = DSP_FS_DEFAULT;
  DSP_LINK->block_size     = DSP_BLOCK_SIZE;
  DSP_LINK->cmd_seq        = 1u;
  DSP_LINK->cmd_ack        = 0u;
  DSP_LINK->cmd_run        = 1u;
  DSP_LINK->cmd_fs_hz      = DSP_FS_DEFAULT;

  __DSB();
  DSP_LINK->magic = DSP_LINK_MAGIC;
  __DSB();

  printf("[IPC] CM4 desbloqueado con DSP_LINK_MAGIC (0x%08lX)\r\n", DSP_LINK_MAGIC);
  printf("[IPC] Parametros: fs=%lu Hz, N=%lu, VREFINT_CAL=%lu\r\n",
         DSP_LINK->fs_hz, DSP_LINK->block_size, DSP_LINK->vrefint_cal);

  /* 6. Inicializar motor CMSIS-DSP y DWT Wavelet */
  printf("[DSP] Inicializando motor CMSIS-DSP (Blackman-Harris 4T + RFFT-2048)...\r\n");
  DSP_Engine_Init();
  printf("[DSP] Inicializando motor Wavelet DWT Mallat (Daubechies-4, 4 niveles)...\r\n");
  DSP_Wavelet_Init();
  printf("[DSP] Motores listos.\r\n");

  /* 7. Inicializar pila lwIP y supervisor de enlace */
  printf("[NET] Inicializando lwIP y MAC Ethernet...\r\n");
  MX_LWIP_Init();
  APP_Ethernet_Init();

  uint32_t last_processed_block = 0;
  uint32_t last_stat_print = HAL_GetTick();

  /* 8. Lazo principal */
  for (;;)
  {
    /* Atender pila lwIP y supervisor de red */
    MX_LWIP_Process();
    APP_Ethernet_Process();

    /* Comprobar si el Cortex-M4 ha completado un nuevo bloque Ping-Pong */
    uint32_t current_block = DSP_LINK->block_id;
    if (current_block != last_processed_block)
    {
      last_processed_block = current_block;
      uint32_t ready_idx = DSP_LINK->ready_idx;

      /* 1. Procesar bloque con CMSIS-DSP (toma ~60 us en M7 a 400 MHz) */
      DSP_AnalysisResult_t res;
      DSP_Engine_ProcessBlock(DSP_LINK->raw_buffer[ready_idx], DSP_LINK->fs_hz, &res);

      /* 2. Procesar bloque con DWT Wavelet Mallat db4 (toma ~70 us en M7 a 400 MHz) */
      DSP_WaveletResult_t dwt_res;
      float32_t mean_code = (res.dc_offset_mv * 65535.0f) / 3300.0f;
      DSP_Wavelet_ProcessBlock(DSP_LINK->raw_buffer[ready_idx], DSP_LINK->fs_hz, mean_code, &dwt_res);

      /* 3. Si hay un cliente UDP escuchando, transmitir paquete de telemetria combinado */
      if (UDP_DSP_HasSubscribers())
      {
        UDP_DSP_SendTelemetry(&res, &dwt_res, current_block, DSP_LINK->t_block_ms);
      }
    }

    /* Impresion periodica de diagnostico en consola cada 2 segundos */
    if ((HAL_GetTick() - last_stat_print) >= 2000u)
    {
      last_stat_print = HAL_GetTick();
      Board_IO_ToggleLed(2); /* Parpadeo de latido en LD2 */

      if (DSP_LINK->fault_flag == DSP_FAULT_MAGIC)
      {
        printf("\r\n[M4] *** ALERTA: HARDFAULT EN CORTEX-M4 ***\r\n");
        printf("     CFSR: 0x%08lX, HFSR: 0x%08lX, BFAR: 0x%08lX\r\n",
               DSP_LINK->fault_cfsr, DSP_LINK->fault_hfsr, DSP_LINK->fault_bfar);
        printf("     PC: 0x%08lX, LR: 0x%08lX\r\n",
               DSP_LINK->fault_pc, DSP_LINK->fault_lr);
      }
      else
      {
        uint32_t f0_x10 = DSP_LINK->f0_hz_x10;
        uint32_t thd_x10 = DSP_LINK->thd_pct_x10;
        int det = (DSP_LINK->flags & DSP_FLAG_SIGNAL_DETECTED) ? 1 : 0;
        uint32_t d4_x10 = DSP_LINK->dwt_energy_pct_x10[3];

        printf("[DSP] Blk %6lu | %s | f0: %5lu.%1lu Hz | Vpk: %4lu mV | Vdc: %4lu mV | THD: %3lu.%1lu %% | D4_E: %3lu.%1lu %% | Trans: %lu\r\n",
               DSP_LINK->block_id,
               det ? "PERIODICO" : "   RUIDO ",
               f0_x10 / 10u, f0_x10 % 10u,
               DSP_LINK->f0_mag_mv,
               DSP_LINK->dc_offset_mv,
               thd_x10 / 10u, thd_x10 % 10u,
               d4_x10 / 10u, d4_x10 % 10u,
               DSP_LINK->dwt_transient_count);
      }
    }
  }
}

static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState       = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM       = 4;
  RCC_OscInitStruct.PLL.PLLN       = 400;
  RCC_OscInitStruct.PLL.PLLP       = 2;
  RCC_OscInitStruct.PLL.PLLQ       = 4;
  RCC_OscInitStruct.PLL.PLLR       = 2;
  RCC_OscInitStruct.PLL.PLLRGE     = RCC_PLL1VCIRANGE_1;
  RCC_OscInitStruct.PLL.PLLVCOSEL  = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN   = 0;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);

  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                     RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                                     RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4);
}

static void MX_GPIO_Init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /* Pines del RMII para LAN8742A */
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull      = GPIO_NOPULL;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF11_ETH;

  /* PA1: REF_CLK, PA2: MDIO, PA7: CRS_DV */
  GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* PC1: MDC, PC4: RXD0, PC5: RXD1 */
  GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* PG11: TX_EN, PG13: TXD0 */
  GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_13;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /* PB13: TXD1 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* PD8 (TX) y PD9 (RX): ST-LINK VCP USART3 */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  GPIO_InitStruct.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull      = GPIO_NOPULL;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
