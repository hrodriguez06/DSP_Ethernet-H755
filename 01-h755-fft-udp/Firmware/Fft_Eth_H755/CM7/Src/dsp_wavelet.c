/**
  ******************************************************************************
  * @file    dsp_wavelet.c
  * @brief   Motor de Transformada Wavelet Discreta (DWT Mallat con Daubechies-4)
  *          para analisis multirresolucion de energia y deteccion instantanea
  *          de transitorios en Cortex-M7.
  ******************************************************************************
  */

#include "dsp_wavelet.h"
#include <math.h>

/* Coeficientes exactos del filtro ortonormal Daubechies-4 (8 coeficientes)
 * Propiedades:
 *   sum(H) = sqrt(2), sum(H^2) = 1.0
 *   sum(G) = 0, sum(G^2) = 1.0, dot(H, G) = 0
 *   Conserva la energia exacta de Parseval entre dominios.
 */
static const float32_t H_DEC[8] = {
  -0.010597401785f,
   0.032883011667f,
   0.030841381836f,
  -0.187034811719f,
  -0.027983769417f,
   0.630880767930f,
   0.714846570553f,
   0.230377813309f
};

static const float32_t G_DEC[8] = {
   0.230377813309f,
  -0.714846570553f,
   0.630880767930f,
   0.027983769417f,
  -0.187034811719f,
  -0.030841381836f,
   0.032883011667f,
   0.010597401785f
};

/* Buffers de trabajo estáticos en memoria rápida AXI SRAM */
static float32_t s_approx_a[DSP_BLOCK_SIZE];     /* 2048 elementos (8 KB) */
static float32_t s_approx_b[DSP_BLOCK_SIZE / 2]; /* 1024 elementos (4 KB) */
static float32_t s_detail[DSP_BLOCK_SIZE / 2];   /* 1024 elementos (4 KB) */

/* Contador acumulativo persistente de transitorios detectados */
static uint32_t s_transient_event_counter = 0;

void DSP_Wavelet_Init(void)
{
  s_transient_event_counter = 0;
}

void DSP_Wavelet_ProcessBlock(const volatile uint16_t *raw_samples,
                             uint32_t fs_hz,
                             float32_t mean_code,
                             DSP_WaveletResult_t *out_result)
{
  if (fs_hz == 0u) fs_hz = DSP_FS_DEFAULT;

  /* Factor de conversión: 16 bits = 65535 LSB -> 3300.0 mV */
  const float32_t lsb_to_mv = 3300.0f / 65535.0f;

  /* 1. Cargar señal eliminando el offset DC a s_approx_a (sin ventaneo Hanning,
   *    para no distorsionar ni atenuar transitorios en los extremos del bloque) */
  for (uint32_t i = 0; i < DSP_BLOCK_SIZE; i++)
  {
    s_approx_a[i] = ((float32_t)raw_samples[i] - mean_code) * lsb_to_mv;
  }

  float32_t energy_bands[DWT_NUM_BANDS] = {0.0f};
  uint32_t  band_lengths[DWT_NUM_BANDS] = {1024u, 512u, 256u, 128u, 128u};

  float32_t *current_in = s_approx_a;
  float32_t *next_approx = s_approx_b;
  uint32_t current_len = DSP_BLOCK_SIZE; /* 2048 */

  /* Variables para deteccion de transitorios en Nivel 1 (D1) */
  float32_t d1_max_abs = 0.0f;
  uint32_t  d1_max_idx = 0;

  /* 2. Descomposicion piramidal de Mallat a 4 niveles */
  for (uint32_t lvl = 0; lvl < DWT_LEVELS; lvl++)
  {
    uint32_t half_len = current_len >> 1;
    uint32_t mask = current_len - 1u; /* Mascara para modulo rapido circular */

    float32_t sum_sq_detail = 0.0f;
    float32_t sum_sq_approx = 0.0f;

    for (uint32_t n = 0; n < half_len; n++)
    {
      float32_t sum_a = 0.0f;
      float32_t sum_d = 0.0f;
      uint32_t base_idx = 2u * n;

      for (uint32_t k = 0; k < 8; k++)
      {
        uint32_t idx = (base_idx + k) & mask;
        float32_t val = current_in[idx];
        sum_a += H_DEC[k] * val;
        sum_d += G_DEC[k] * val;
      }

      next_approx[n] = sum_a;
      s_detail[n]    = sum_d;

      sum_sq_detail += (sum_d * sum_d);

      /* Monitoreo especializado en D1 (6.4 - 12.8 kHz) para picos anómalos */
      if (lvl == 0)
      {
        float32_t abs_d = fabsf(sum_d);
        if (abs_d > d1_max_abs)
        {
          d1_max_abs = abs_d;
          d1_max_idx = n;
        }
      }

      if (lvl == (DWT_LEVELS - 1u))
      {
        sum_sq_approx += (sum_a * sum_a);
      }
    }

    /* Guardar energia del detalle de este nivel */
    energy_bands[lvl] = sum_sq_detail;

    /* Si es el ultimo nivel, guardar tambien la energia de aproximacion A4 */
    if (lvl == (DWT_LEVELS - 1u))
    {
      energy_bands[DWT_BAND_A4] = sum_sq_approx;
    }

    /* Preparar buffers para la siguiente iteración (reutilizar memoria) */
    if (lvl == 0)
    {
      /* Nivel 2 lee de s_approx_b y escribe en s_approx_a */
      current_in = s_approx_b;
      next_approx = s_approx_a;
    }
    else if (lvl == 1)
    {
      /* Nivel 3 lee de s_approx_a y escribe en s_approx_b */
      current_in = s_approx_a;
      next_approx = s_approx_b;
    }
    else if (lvl == 2)
    {
      /* Nivel 4 lee de s_approx_b y escribe en s_approx_a */
      current_in = s_approx_b;
      next_approx = s_approx_a;
    }

    current_len = half_len;
  }

  /* 3. Calculo de Energia Total, Porcentajes, Densidad y Entropia */
  float32_t total_energy = 0.0f;
  for (uint32_t b = 0; b < DWT_NUM_BANDS; b++)
  {
    total_energy += energy_bands[b];
    out_result->rms_mv[b] = sqrtf(energy_bands[b] / (float32_t)band_lengths[b]);
  }
  out_result->total_energy = total_energy;

  /* Anchos de banda en Hz de cada nivel */
  float32_t delta_f[DWT_NUM_BANDS];
  delta_f[0] = (float32_t)fs_hz / 4.0f;  /* D1: 6400 Hz para Fs=25.6 kHz */
  delta_f[1] = (float32_t)fs_hz / 8.0f;  /* D2: 3200 Hz */
  delta_f[2] = (float32_t)fs_hz / 16.0f; /* D3: 1600 Hz */
  delta_f[3] = (float32_t)fs_hz / 32.0f; /* D4: 800 Hz */
  delta_f[4] = (float32_t)fs_hz / 32.0f; /* A4: 800 Hz */

  float32_t entropy = 0.0f;
  if (total_energy > 1e-4f)
  {
    for (uint32_t b = 0; b < DWT_NUM_BANDS; b++)
    {
      float32_t p = energy_bands[b] / total_energy;
      out_result->energy_pct[b]     = p * 100.0f;
      out_result->energy_density[b] = energy_bands[b] / delta_f[b];

      if (p > 1e-6f)
      {
        entropy -= p * logf(p);
      }
    }
  }
  else
  {
    for (uint32_t b = 0; b < DWT_NUM_BANDS; b++)
    {
      out_result->energy_pct[b]     = 0.0f;
      out_result->energy_density[b] = 0.0f;
    }
  }

  out_result->entropy_shannon = entropy;
  /* Entropia normalizada al maximo teorico de 5 estados: ln(5) = 1.6094379f */
  out_result->entropy_norm    = entropy / 1.6094379f;
  if (out_result->entropy_norm > 1.0f) out_result->entropy_norm = 1.0f;

  /* 4. Analisis de Transitorios y Anomalías en D1 (6.4 - 12.8 kHz) */
  float32_t sigma_d1 = out_result->rms_mv[DWT_BAND_D1];
  float32_t crest_d1 = (sigma_d1 > 0.01f) ? (d1_max_abs / sigma_d1) : 0.0f;

  out_result->d1_max_mv       = d1_max_abs;
  out_result->d1_crest_factor = crest_d1;

  /* Criterio de disparo de transitorio:
   * 1. El pico de D1 debe superar 4.5 veces el nivel eficaz de fondo de D1.
   * 2. El pico absoluto de D1 debe superar 35 mV (filtra ruido termico o LSBs residuales).
   */
  if ((crest_d1 > 4.5f) && (d1_max_abs > 35.0f))
  {
    s_transient_event_counter++;
    out_result->transient_detected = 1;
    out_result->transient_mag_mv   = d1_max_abs;
    out_result->transient_sample   = d1_max_idx * 2u;
    out_result->transient_time_us  = (out_result->transient_sample * 1000000u) / fs_hz;
  }
  else
  {
    out_result->transient_detected = 0;
    out_result->transient_mag_mv   = 0.0f;
    out_result->transient_sample   = 0u;
    out_result->transient_time_us  = 0u;
  }

  out_result->transient_count = s_transient_event_counter;
}
