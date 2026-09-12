/**
  ******************************************************************************
  * @file    dsp_link.h
  * @brief   Contrato IPC en SRAM4 (0x38000000) entre el Cortex-M4 (adquisicion
  *          periodica determinista) y el Cortex-M7 (FFT, CMSIS-DSP y lwIP UDP).
  *
  *  Disposicion en SRAM4:
  *      0x000 - 0x03F : Contrato de arranque (M7 escribe una vez, M4 lee)
  *      0x040 - 0x07F : Mando (M7 pide comandos, M4 aplica y acusa)
  *      0x080 - 0x1FF : Estado, marcas y diagnostico de FFT publicado por M7
  *      0x200 - 0x9FF : Buffer Ping-Pong 0 (1024 muestras uint16_t = 2048 B)
  *      0xA00 - 0x11FF: Buffer Ping-Pong 1 (1024 muestras uint16_t = 2048 B)
  *
  *  Reglas de coherencia:
  *    - M7: Región configurada en MPU como MPU_ACCESS_NOT_CACHEABLE.
  *    - M4: __DSB() antes de alternar flags de bloque listo.
  ******************************************************************************
  */

#ifndef DSP_LINK_H
#define DSP_LINK_H

#include <stddef.h>
#include <stdint.h>

#define DSP_LINK_BASE       0x38000000UL
#define DSP_LINK_SIZE       0x10000UL      /* 64 KB (SRAM4 entera) */
#define DSP_LINK_MAGIC      0x44535031UL   /* "DSP1" */
#define DSP_LINK_VERSION    1u

#define DSP_FAULT_MAGIC     0xDEADFA11UL   /* Marca de HardFault en M4 */

#define DSP_BLOCK_SIZE      2048u          /* 2048 muestras por bloque (df = 12.5 Hz) */
#define DSP_FS_DEFAULT      25600u         /* 25.6 kHz */

#if defined(CORE_CM7)
#define DSP_VREFINT_CAL_ADDR   ((const uint16_t *)0x1FF1E860UL)
#endif

/* Flags de estado del motor DSP */
#define DSP_FLAG_RUNNING         (1u << 0)
#define DSP_FLAG_SIGNAL_DETECTED (1u << 1)
#define DSP_FLAG_CLIPPING        (1u << 2)
#define DSP_FLAG_OVERRUN         (1u << 3)

typedef struct
{
  /* --- 0x000: Arranque (CM7 escribe una vez, CM4 espera magic) ------------ */
  volatile uint32_t magic;           /**< DSP_LINK_MAGIC cuando el M7 termina  */
  volatile uint32_t version;         /**< DSP_LINK_VERSION                     */
  volatile uint32_t vrefint_cal;     /**< Calibracion de fabrica 16-bit        */
  volatile uint32_t sysclk_hz;       /**< 400 MHz                              */
  volatile uint32_t hclk_hz;         /**< 200 MHz                              */
  volatile uint32_t apb1_timer_hz;   /**< Frecuencia de reloj de TIM3          */
  volatile uint32_t fs_hz;           /**< Frecuencia de muestreo (25600 Hz)    */
  volatile uint32_t block_size;      /**< 1024                                 */
  volatile uint32_t rsv0[8];         /**< Relleno hasta 0x040                  */

  /* --- 0x040: Mando (CM7 solicita, CM4 acusa) ----------------------------- */
  volatile uint32_t cmd_seq;         /**< Incrementado por CM7 al ordenar      */
  volatile uint32_t cmd_ack;         /**< Copiado por CM4 tras aplicar         */
  volatile uint32_t cmd_run;         /**< 1 = muestreo activo, 0 = detenido    */
  volatile uint32_t cmd_fs_hz;       /**< Nueva fs deseada                     */
  volatile uint32_t rsv1[12];        /**< Relleno hasta 0x080                  */

  /* --- 0x080: Sincronizacion y Telemetria (CM4 publica estado de captura) -- */
  volatile uint32_t block_id;        /**< Contador incremental de bloques      */
  volatile uint32_t ready_idx;       /**< Indice del buffer lleno (0 o 1)      */
  volatile uint32_t active_idx;      /**< Indice del buffer en llenado (0 o 1) */
  volatile uint32_t sample_count;    /**< Muestras capturadas en bloque actual */
  volatile uint32_t t_block_ms;      /**< SysTick CM4 al completar bloque      */
  volatile uint32_t overrun_count;   /**< Bloques sobreescritos sin procesar   */
  volatile uint32_t flags;           /**< DSP_FLAG_*                           */

  /* --- 0x09C: Metricas DSP calculadas por CM7 (para consulta/debug) -------- */
  volatile uint32_t f0_hz_x10;       /**< Fundamental en decimas de Hz (ej 10000 = 1000.0) */
  volatile uint32_t f0_mag_mv;       /**< Amplitud pico de f0 en mV            */
  volatile uint32_t noise_floor_mv;  /**< Piso de ruido en mV                  */
  volatile uint32_t snr_db_x10;      /**< SNR en decimas de dB                 */
  volatile uint32_t thd_pct_x10;     /**< THD en decimas de % (ej 25 = 2.5%)   */
  volatile uint32_t harm_mag_mv[10]; /**< Amplitud armónicos 1 a 10            */
  volatile uint32_t dc_offset_mv;    /**< Componente continua (Offset DC) en mV */
  volatile uint32_t v_pp_mv;         /**< Voltaje pico a pico medido en mV     */

  /* --- 0x0E0: Trampa de fallos CM4 ---------------------------------------- */
  volatile uint32_t fault_flag;      /**< DSP_FAULT_MAGIC si hubo HardFault    */
  volatile uint32_t fault_cfsr;
  volatile uint32_t fault_hfsr;
  volatile uint32_t fault_bfar;
  volatile uint32_t fault_pc;
  volatile uint32_t fault_lr;
  volatile uint32_t rsv2[66];        /**< Relleno exacto hasta offset 0x200    */

  /* --- 0x200: Buffer Ping-Pong (2 x 1024 uint16_t = 4096 bytes) ----------- */
  /* Buffer 0: 0x38000200 - 0x380009FF (2048 bytes)
     Buffer 1: 0x38000A00 - 0x380011FF (2048 bytes) */
  volatile uint16_t raw_buffer[2][DSP_BLOCK_SIZE];

} DSP_Link_t;

#define DSP_LINK  ((DSP_Link_t *)DSP_LINK_BASE)

/* Verificacion estatica en compilacion: garantizar offset de buffers a 0x200 */
#if defined(__GNUC__)
_Static_assert(offsetof(DSP_Link_t, raw_buffer) == 0x200, "raw_buffer debe iniciar exactamente en offset 0x200");
_Static_assert(sizeof(DSP_Link_t) <= DSP_LINK_SIZE, "DSP_Link_t excede tamano de SRAM4");
#endif

#endif /* DSP_LINK_H */
