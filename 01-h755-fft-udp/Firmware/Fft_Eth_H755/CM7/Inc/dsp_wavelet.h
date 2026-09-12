/**
  ******************************************************************************
  * @file    dsp_wavelet.h
  * @brief   Motor de Transformada Wavelet Discreta (DWT Mallat con Daubechies-4)
  *          para analisis multirresolucion de energia y deteccion instantanea
  *          de transitorios en Cortex-M7.
  ******************************************************************************
  */

#ifndef DSP_WAVELET_H
#define DSP_WAVELET_H

#include <stdint.h>
#include "arm_math.h"
#include "dsp_link.h"

#define DWT_LEVELS              4u
#define DWT_NUM_BANDS           5u  /* D1, D2, D3, D4, A4 */

/* Indices de las bandas en los arreglos de energia/RMS */
#define DWT_BAND_D1             0u  /* 6.4 kHz - 12.8 kHz */
#define DWT_BAND_D2             1u  /* 3.2 kHz - 6.4 kHz  */
#define DWT_BAND_D3             2u  /* 1.6 kHz - 3.2 kHz  */
#define DWT_BAND_D4             3u  /* 800 Hz  - 1.6 kHz  (Fundamental f0) */
#define DWT_BAND_A4             4u  /* 0 Hz    - 800 Hz   (DC y subarmónicos) */

typedef struct
{
  /* Distribución de energía y nivel eficaz por banda */
  float32_t energy_pct[DWT_NUM_BANDS];     /**< Porcentaje de energia 0.0 - 100.0 % */
  float32_t rms_mv[DWT_NUM_BANDS];         /**< Amplitud RMS de cada banda en mV    */
  float32_t energy_density[DWT_NUM_BANDS]; /**< Densidad espectral de energia (mV^2/Hz) */
  float32_t total_energy;                 /**< Energia total del bloque             */
  float32_t entropy_shannon;              /**< Entropia de Shannon (0 a ln(5)=1.61) */
  float32_t entropy_norm;                 /**< Entropia normalizada (0.0 a 1.0)     */

  /* Deteccion y localizacion de transitorios / anomalías */
  int       transient_detected;       /**< 1 si se detecto un disturbio en D1   */
  float32_t transient_mag_mv;         /**< Amplitud pico del glitch en mV       */
  uint32_t  transient_sample;         /**< Indice de muestra (0..2047)          */
  uint32_t  transient_time_us;        /**< Offset temporal dentro del bloque    */
  uint32_t  transient_count;          /**< Contador acumulativo de eventos      */
  float32_t d1_crest_factor;          /**< Factor de cresta en D1 (Peak / RMS)  */
  float32_t d1_max_mv;                /**< Maximo coeficiente absoluto en D1    */
} DSP_WaveletResult_t;

/**
  * @brief  Inicializa el motor Wavelet y limpia contadores.
  */
void DSP_Wavelet_Init(void);

/**
  * @brief  Ejecuta la DWT de 4 niveles sobre las 2048 muestras en crudo del buffer.
  * @param  raw_samples  Vector de 2048 muestras uint16_t de ADC.
  * @param  fs_hz        Frecuencia de muestreo (ej. 25600 Hz).
  * @param  mean_code    Nivel medio (DC offset en LSB) calculado por DSP_Engine.
  * @param  out_result   Estructura receptora con las metricas de bandas y transitorios.
  */
void DSP_Wavelet_ProcessBlock(const volatile uint16_t *raw_samples,
                             uint32_t fs_hz,
                             float32_t mean_code,
                             DSP_WaveletResult_t *out_result);

#endif /* DSP_WAVELET_H */
