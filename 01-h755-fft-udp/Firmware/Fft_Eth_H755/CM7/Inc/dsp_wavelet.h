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

#define WPT_LEVELS              4u
#define WPT_NUM_BANDS           16u  /* 16 sub-bandas uniformes B0..B15 */
#define WPT_PACKET_LEN          (DSP_BLOCK_SIZE / WPT_NUM_BANDS) /* 256 muestras por paquete */
#define WPT_BAND_WIDTH_HZ       800.0f /* 800 Hz constantes por sub-banda */

typedef struct
{
  /* Distribución de energía y nivel eficaz por banda (16 bandas ordenadas de 0 a 12.8 kHz) */
  float32_t energy_pct[WPT_NUM_BANDS];     /**< Porcentaje de energia 0.0 - 100.0 % */
  float32_t rms_mv[WPT_NUM_BANDS];         /**< Amplitud RMS de cada banda en mV    */
  float32_t energy_density[WPT_NUM_BANDS]; /**< Densidad espectral de energia (mV^2/Hz) */
  float32_t total_energy;                  /**< Energia total del bloque             */
  float32_t entropy_shannon;               /**< Entropia de Shannon (0 a ln(16)=2.77) */
  float32_t entropy_norm;                  /**< Entropia normalizada (0.0 a 1.0)     */

  /* Deteccion y localizacion de transitorios / anomalías */
  int       transient_detected;       /**< 1 si se detecto un disturbio en HF   */
  float32_t transient_mag_mv;         /**< Amplitud pico del glitch en mV       */
  uint32_t  transient_sample;         /**< Indice de muestra (0..4095)          */
  uint32_t  transient_time_us;        /**< Offset temporal dentro del bloque    */
  uint32_t  transient_count;          /**< Contador acumulativo de eventos      */
  float32_t crest_factor;             /**< Factor de cresta en banda HF         */
  float32_t hf_max_mv;                /**< Maximo coeficiente absoluto en HF    */
} DSP_WaveletResult_t;

/**
  * @brief  Inicializa el motor Wavelet WPT y limpia contadores.
  */
void DSP_Wavelet_Init(void);

/**
  * @brief  Ejecuta la WPT de 4 niveles sobre las 4096 muestras en crudo del buffer,
  *         generando 16 sub-bandas de frecuencia uniformes de 800 Hz.
  * @param  raw_samples  Vector de 4096 muestras uint16_t de ADC.
  * @param  fs_hz        Frecuencia de muestreo (ej. 25600 Hz).
  * @param  mean_code    Nivel medio (DC offset en LSB) calculado por DSP_Engine.
  * @param  out_result   Estructura receptora con las metricas de 16 bandas y transitorios.
  */
void DSP_Wavelet_ProcessBlock(const volatile uint16_t *raw_samples,
                             uint32_t fs_hz,
                             float32_t mean_code,
                             DSP_WaveletResult_t *out_result);

#endif /* DSP_WAVELET_H */
