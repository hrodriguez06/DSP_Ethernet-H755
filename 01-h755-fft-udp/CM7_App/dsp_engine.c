/**
  ******************************************************************************
  * @file    dsp_engine.c
  * @brief   Motor de analisis espectral CMSIS-DSP y deteccion de armonicos
  *          en Cortex-M7 para 3_DSP_Ethernet.
  ******************************************************************************
  */

#include "dsp_engine.h"
#include "arm_math.h"
#include <math.h>

static arm_rfft_fast_instance_f32 s_rfft_instance;

/* Buffers de cálculo en AXI SRAM (D1, rápida y cacheada) */
static float32_t s_window[DSP_BLOCK_SIZE];
static float32_t s_input_time[DSP_BLOCK_SIZE];
static float32_t s_fft_output[DSP_BLOCK_SIZE];
static float32_t s_mag[DSP_NUM_BINS];

void DSP_Engine_Init(void)
{
  /* 1. Inicializar la instancia RFFT para DSP_BLOCK_SIZE puntos (2048) */
  arm_rfft_fast_init_f32(&s_rfft_instance, DSP_BLOCK_SIZE);

  /* 2. Precalcular la ventana Blackman-Harris de 4 términos (-92 dB de atenuación de lóbulos):
        w[n] = a0 - a1*cos(2*pi*n/(N-1)) + a2*cos(4*pi*n/(N-1)) - a3*cos(6*pi*n/(N-1)) */
  const float32_t a0 = 0.35875f;
  const float32_t a1 = 0.48829f;
  const float32_t a2 = 0.14128f;
  const float32_t a3 = 0.01168f;
  const float32_t two_pi = 6.28318530717958647692f;
  const float32_t inv_nm1 = 1.0f / (float32_t)(DSP_BLOCK_SIZE - 1u);

  for (uint32_t n = 0; n < DSP_BLOCK_SIZE; n++)
  {
    float32_t phi = two_pi * (float32_t)n * inv_nm1;
    s_window[n] = a0 - a1 * cosf(phi) + a2 * cosf(2.0f * phi) - a3 * cosf(3.0f * phi);
  }
}

const float *DSP_Engine_GetMagnitudeSpectrum(void)
{
  return s_mag;
}

void DSP_Engine_ProcessBlock(const volatile uint16_t *raw_samples,
                             uint32_t fs_hz,
                             DSP_AnalysisResult_t *out_result)
{
  if (fs_hz == 0u) fs_hz = DSP_FS_DEFAULT;

  /* 1. Calcular media de la señal en crudo (DC offset) y valores min/max */
  float32_t sum = 0.0f;
  uint16_t min_raw = raw_samples[0];
  uint16_t max_raw = raw_samples[0];

  for (uint32_t i = 0; i < DSP_BLOCK_SIZE; i++)
  {
    uint16_t s = raw_samples[i];
    sum += (float32_t)s;
    if (s < min_raw) min_raw = s;
    if (s > max_raw) max_raw = s;
  }
  float32_t mean_code = sum / (float32_t)DSP_BLOCK_SIZE;

  /* Factor de conversión: 16 bits = 65535 LSB -> 3300.0 mV */
  const float32_t lsb_to_mv = 3300.0f / 65535.0f;
  out_result->dc_offset_mv = mean_code * lsb_to_mv;
  out_result->v_pp_mv      = (float32_t)(max_raw - min_raw) * lsb_to_mv;

  /* 2. Restar componente continua (DC) y aplicar ventana Blackman-Harris */
  for (uint32_t i = 0; i < DSP_BLOCK_SIZE; i++)
  {
    float32_t ac_mv = ((float32_t)raw_samples[i] - mean_code) * lsb_to_mv;
    s_input_time[i] = ac_mv * s_window[i];
  }

  /* 3. Ejecutar Real FFT con CMSIS-DSP a 400 MHz */
  arm_rfft_fast_f32(&s_rfft_instance, s_input_time, s_fft_output, 0);

  /* 4. Calcular magnitudes complejas: s_mag[k] = sqrt(Re^2 + Im^2) */
  arm_cmplx_mag_f32(s_fft_output, s_mag, DSP_NUM_BINS);

  /* 5. Normalizar magnitudes a milivoltios pico:
        Para Blackman-Harris 4T, ganancia coherente a0 = 0.35875. Factor espectral = 2 / (a0 * N) */
  const float32_t norm_factor = 2.0f / (0.35875f * (float32_t)DSP_BLOCK_SIZE);
  s_mag[0] = s_mag[0] / (float32_t)DSP_BLOCK_SIZE; /* Bin DC */
  for (uint32_t k = 1; k < DSP_NUM_BINS; k++)
  {
    s_mag[k] *= norm_factor;
  }

  /* 6. Estimación de piso de ruido y búsqueda de fundamental f0:
        Omitimos bins 0 y 1 (< 50 Hz) para no captar deriva térmica/offset */
  uint32_t peak_bin = 2u;
  float32_t peak_mag = s_mag[2];
  float32_t noise_sum = 0.0f;

  for (uint32_t k = 2; k < DSP_NUM_BINS; k++)
  {
    noise_sum += s_mag[k];
    if (s_mag[k] > peak_mag)
    {
      peak_mag = s_mag[k];
      peak_bin = k;
    }
  }

  /* Piso de ruido aproximado (excluyendo el pico dominante) */
  float32_t noise_floor = (noise_sum - peak_mag) / (float32_t)(DSP_NUM_BINS - 3u);
  if (noise_floor < 0.01f) noise_floor = 0.01f;

  float32_t df = (float32_t)fs_hz / (float32_t)DSP_BLOCK_SIZE;

  /* Criterio de detección: señal supera 3x el piso de ruido y es > 3 mV */
  if (peak_mag > (noise_floor * 3.0f) && (peak_mag > 3.0f))
  {
    /* Interpolacion parabolica sub-bin para eliminar el error de cuantizacion de 25 Hz */
    float32_t delta = 0.0f;
    if (peak_bin > 0 && peak_bin < (DSP_NUM_BINS - 1u))
    {
      float32_t y1 = s_mag[peak_bin - 1u];
      float32_t y2 = s_mag[peak_bin];
      float32_t y3 = s_mag[peak_bin + 1u];
      float32_t denom = 2.0f * (2.0f * y2 - y1 - y3);
      if (denom > 1e-4f)
      {
        delta = (y3 - y1) / denom;
        if (delta > 0.5f) delta = 0.5f;
        if (delta < -0.5f) delta = -0.5f;
      }
    }

    out_result->signal_detected = 1;
    out_result->f0_hz           = ((float32_t)peak_bin + delta) * df;
    out_result->f0_mag_mv       = peak_mag;
    out_result->noise_floor_mv  = noise_floor;
    out_result->snr_db          = 20.0f * log10f(peak_mag / noise_floor);

    /* Seguimiento de los armónicos 2f0 .. 10f0 */
    float32_t harm_power_sum = 0.0f;
    out_result->harm_mag_mv[0] = peak_mag; /* Fundamental */

    for (uint32_t h = 2; h <= 10; h++)
    {
      uint32_t center_bin = peak_bin * h;
      float32_t h_mag = 0.0f;

      if (center_bin < (DSP_NUM_BINS - 1u))
      {
        /* Buscar máximo en ventana local +/- 1 bin por dispersión */
        h_mag = s_mag[center_bin];
        if (center_bin > 0 && s_mag[center_bin - 1u] > h_mag)
        {
          h_mag = s_mag[center_bin - 1u];
        }
        if ((center_bin + 1u) < DSP_NUM_BINS && s_mag[center_bin + 1u] > h_mag)
        {
          h_mag = s_mag[center_bin + 1u];
        }
      }

      out_result->harm_mag_mv[h - 1u] = h_mag;
      harm_power_sum += (h_mag * h_mag);
    }

    /* Distorsión Armónica Total (THD %): sqrt(V2^2 + V3^2 + ... + V10^2) / V1 * 100 */
    out_result->thd_pct = (sqrtf(harm_power_sum) / peak_mag) * 100.0f;
  }
  else
  {
    /* Solo ruido detectado */
    out_result->signal_detected = 0;
    out_result->f0_hz           = 0.0f;
    out_result->f0_mag_mv       = 0.0f;
    out_result->noise_floor_mv  = noise_floor;
    out_result->snr_db          = 0.0f;
    out_result->thd_pct         = 0.0f;
    for (int h = 0; h < 10; h++)
    {
      out_result->harm_mag_mv[h] = 0.0f;
    }
  }

  /* 7. Publicar resultados procesados en el contrato SRAM4 */
  DSP_LINK->f0_hz_x10      = (uint32_t)(out_result->f0_hz * 10.0f);
  DSP_LINK->f0_mag_mv      = (uint32_t)out_result->f0_mag_mv;
  DSP_LINK->noise_floor_mv = (uint32_t)out_result->noise_floor_mv;
  DSP_LINK->snr_db_x10     = (uint32_t)(out_result->snr_db * 10.0f);
  DSP_LINK->thd_pct_x10    = (uint32_t)(out_result->thd_pct * 10.0f);

  for (int h = 0; h < 10; h++)
  {
    DSP_LINK->harm_mag_mv[h] = (uint32_t)out_result->harm_mag_mv[h];
  }

  DSP_LINK->dc_offset_mv   = (uint32_t)out_result->dc_offset_mv;
  DSP_LINK->v_pp_mv        = (uint32_t)out_result->v_pp_mv;

  if (out_result->signal_detected)
  {
    DSP_LINK->flags |= DSP_FLAG_SIGNAL_DETECTED;
  }
  else
  {
    DSP_LINK->flags &= ~DSP_FLAG_SIGNAL_DETECTED;
  }
  __DSB();
}
