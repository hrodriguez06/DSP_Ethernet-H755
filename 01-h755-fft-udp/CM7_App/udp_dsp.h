/**
  ******************************************************************************
  * @file    udp_dsp.h
  * @brief   Servidor y emisor UDP de telemetria espectral (puerto 5002) en CM7.
  ******************************************************************************
  */

#ifndef UDP_DSP_H
#define UDP_DSP_H

#include <stdint.h>
#include "dsp_engine.h"

#define UDP_DSP_PORT            5002u
#define UDP_DSP_MAGIC_TELEMETRY 0x44535032UL /* "DSP2" */
#define UDP_DSP_MAGIC_SPECTRUM  0x53504543UL /* "SPEC" */

typedef struct __attribute__((packed))
{
  uint32_t magic;           /**< UDP_DSP_MAGIC_TELEMETRY                     */
  uint32_t block_id;        /**< Contador incremental de bloque              */
  uint32_t timestamp_ms;    /**< SysTick al completar muestreo               */
  uint32_t fs_hz;           /**< Frecuencia de muestreo (25600 Hz)           */
  uint32_t n_fft;           /**< 1024                                        */
  uint32_t f0_hz_x10;       /**< Fundamental en decimas de Hz                */
  uint32_t f0_mag_mv;       /**< Amplitud fundamental en mV                  */
  uint32_t noise_floor_mv;  /**< Piso de ruido en mV                         */
  uint32_t snr_db_x10;      /**< Relacion senal/ruido en decimas de dB       */
  uint32_t thd_pct_x10;     /**< Distorsion armonica en decimas de %         */
  uint32_t flags;           /**< Flags de estado                             */
  uint16_t harm_mag_mv[10]; /**< Magnitudes armonicos 1..10 (20 B)           */
  uint16_t dc_offset_mv;    /**< Componente continua (Offset DC en mV)        */
  uint16_t v_pp_mv;         /**< Voltaje pico a pico en mV                   */
  uint16_t rsv[1];          /**< Alineacion fija a 70 bytes exactos          */
} UDP_DSP_TelemetryPacket_t;

/**
  * @brief  Inicializa socket UDP en puerto 5002 con lwIP RAW.
  */
void UDP_DSP_Init(void);

/**
  * @brief  Transmite datagrama binario compacto de telemetria espectral (64 bytes).
  */
void UDP_DSP_SendTelemetry(const DSP_AnalysisResult_t *result,
                           uint32_t block_id,
                           uint32_t timestamp_ms);

/**
  * @brief  Transmite el vector completo de 512 bins de magnitud en milivoltios.
  */
void UDP_DSP_SendFullSpectrum(const float *mag_spectrum, uint32_t block_id);

/**
  * @brief  Verifica si hay clientes activos suscritos a telemetria continua.
  */
int UDP_DSP_HasSubscribers(void);

#endif /* UDP_DSP_H */
