/**
  ******************************************************************************
  * @file    dsp_engine.h
  * @brief   Motor de analisis espectral CMSIS-DSP y deteccion de armonicos
  *          en Cortex-M7 para 3_DSP_Ethernet.
  ******************************************************************************
  */

#ifndef DSP_ENGINE_H
#define DSP_ENGINE_H

#include <stdint.h>
#include "dsp_link.h"

#define DSP_NUM_BINS    (DSP_BLOCK_SIZE / 2)  /* 512 bins espectrales */

typedef struct
{
  float f0_hz;
  float f0_mag_mv;
  float noise_floor_mv;
  float snr_db;
  float thd_pct;
  float harm_mag_mv[10];
  float dc_offset_mv;
  float v_pp_mv;
  int   signal_detected;
} DSP_AnalysisResult_t;

/**
  * @brief  Inicializa instancias de CMSIS-DSP y precalcula ventana Hanning.
  */
void DSP_Engine_Init(void);

/**
  * @brief  Procesa un bloque de 1024 muestras en crudo:
  *         escalado mV -> remocion DC -> ventana Hanning -> RFFT -> magnitud -> picos.
  * @param  raw_samples   Buffer de 1024 muestras uint16_t.
  * @param  fs_hz         Frecuencia de muestreo (ej. 25600 Hz).
  * @param  out_result    Estructura receptora de metricas calculadas.
  */
void DSP_Engine_ProcessBlock(const volatile uint16_t *raw_samples,
                             uint32_t fs_hz,
                             DSP_AnalysisResult_t *out_result);

/**
  * @brief  Retorna puntero al vector de magnitudes espectrales en mV (512 bins).
  */
const float *DSP_Engine_GetMagnitudeSpectrum(void);

#endif /* DSP_ENGINE_H */
