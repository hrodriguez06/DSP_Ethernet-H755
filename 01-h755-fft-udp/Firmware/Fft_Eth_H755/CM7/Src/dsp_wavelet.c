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
static float32_t s_wpt_buf_a[DSP_BLOCK_SIZE]; /* 4096 floats (16 KB) */
static float32_t s_wpt_buf_b[DSP_BLOCK_SIZE]; /* 4096 floats (16 KB) */

/* Tabla de permutación Gray-code (Paley) para ordenar los 16 nodos del árbol
 * en frecuencias estrictamente monótonas:
 * Banda k -> f_k = [k * 800 Hz, (k + 1) * 800 Hz]
 */
static const uint8_t WPT_GRAY_MAP[WPT_NUM_BANDS] = {
   0,  1,  3,  2,  6,  7,  5,  4,
  12, 13, 15, 14, 10, 11,  9,  8
};

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

  /* 1. Cargar señal eliminando el offset DC a s_wpt_buf_a */
  for (uint32_t i = 0; i < DSP_BLOCK_SIZE; i++)
  {
    s_wpt_buf_a[i] = ((float32_t)raw_samples[i] - mean_code) * lsb_to_mv;
  }

  float32_t *p_in  = s_wpt_buf_a;
  float32_t *p_out = s_wpt_buf_b;

  /* Variables para detección de transitorios en Nivel 1 (High-Pass D1: 2048 puntos, 6.4 - 12.8 kHz) */
  float32_t hf_max_abs = 0.0f;
  uint32_t  hf_max_idx = 0;
  float32_t hf_sum_sq  = 0.0f;

  /* 2. Descomposición completa de Paquetes Wavelet (WPT) a 4 niveles */
  for (uint32_t lvl = 0; lvl < WPT_LEVELS; lvl++)
  {
    uint32_t num_nodes  = 1u << lvl;             /* 1, 2, 4, 8 */
    uint32_t parent_len = DSP_BLOCK_SIZE >> lvl; /* 4096, 2048, 1024, 512 */
    uint32_t child_len  = parent_len >> 1;       /* 2048, 1024, 512, 256 */
    uint32_t mask       = parent_len - 1u;

    for (uint32_t node = 0; node < num_nodes; node++)
    {
      const float32_t *node_in = &p_in[node * parent_len];
      float32_t *node_out_low  = &p_out[(2u * node) * child_len];
      float32_t *node_out_high = &p_out[(2u * node + 1u) * child_len];

      for (uint32_t n = 0; n < child_len; n++)
      {
        float32_t sum_low = 0.0f;
        float32_t sum_high = 0.0f;
        uint32_t base_idx = 2u * n;

        for (uint32_t k = 0; k < 8; k++)
        {
          uint32_t idx = (base_idx + k) & mask;
          float32_t val = node_in[idx];
          sum_low  += H_DEC[k] * val;
          sum_high += G_DEC[k] * val;
        }

        node_out_low[n]  = sum_low;
        node_out_high[n] = sum_high;

        /* En nivel 0 -> calculando nivel 1: node_out_high es la banda HF completa (6.4 - 12.8 kHz) */
        if (lvl == 0)
        {
          float32_t abs_h = fabsf(sum_high);
          hf_sum_sq += sum_high * sum_high;
          if (abs_h > hf_max_abs)
          {
            hf_max_abs = abs_h;
            hf_max_idx = n;
          }
        }
      }
    }

    /* Alternar buffers de entrada y salida */
    float32_t *p_tmp = p_in;
    p_in  = p_out;
    p_out = p_tmp;
  }

  /* 3. Cálculo de Energía, RMS y Densidad de las 16 Sub-bandas (WPT) */
  float32_t total_energy = 0.0f;
  float32_t energy_bands[WPT_NUM_BANDS];

  for (uint32_t b = 0; b < WPT_NUM_BANDS; b++)
  {
    uint32_t node_idx = WPT_GRAY_MAP[b];
    const float32_t *pkt = &p_in[node_idx * WPT_PACKET_LEN];

    float32_t sum_sq = 0.0f;
    for (uint32_t m = 0; m < WPT_PACKET_LEN; m++)
    {
      sum_sq += pkt[m] * pkt[m];
    }

    energy_bands[b] = sum_sq;
    total_energy   += sum_sq;
    out_result->rms_mv[b] = sqrtf(sum_sq / (float32_t)WPT_PACKET_LEN);
  }

  out_result->total_energy = total_energy;

  /* Ancho de banda constante: Fs / (2 * 16) = 800 Hz */
  float32_t df = (float32_t)fs_hz / (2.0f * (float32_t)WPT_NUM_BANDS);

  float32_t entropy = 0.0f;
  if (total_energy > 1e-4f)
  {
    for (uint32_t b = 0; b < WPT_NUM_BANDS; b++)
    {
      float32_t p = energy_bands[b] / total_energy;
      out_result->energy_pct[b]     = p * 100.0f;
      out_result->energy_density[b] = energy_bands[b] / df;

      if (p > 1e-6f)
      {
        entropy -= p * logf(p);
      }
    }
  }
  else
  {
    for (uint32_t b = 0; b < WPT_NUM_BANDS; b++)
    {
      out_result->energy_pct[b]     = 0.0f;
      out_result->energy_density[b] = 0.0f;
    }
  }

  out_result->entropy_shannon = entropy;
  /* Entropía normalizada al máximo teórico de 16 estados: ln(16) = 4 * ln(2) = 2.7725887f */
  out_result->entropy_norm = entropy / 2.7725887f;
  if (out_result->entropy_norm > 1.0f) out_result->entropy_norm = 1.0f;

  /* 4. Análisis de Transitorios y Anomalías en banda HF (6.4 - 12.8 kHz) */
  float32_t sigma_hf = sqrtf(hf_sum_sq / 2048.0f);
  float32_t crest_hf = (sigma_hf > 0.01f) ? (hf_max_abs / sigma_hf) : 0.0f;

  out_result->hf_max_mv   = hf_max_abs;
  out_result->crest_factor = crest_hf;

  if ((crest_hf > 4.5f) && (hf_max_abs > 35.0f))
  {
    s_transient_event_counter++;
    out_result->transient_detected = 1;
    out_result->transient_mag_mv   = hf_max_abs;
    out_result->transient_sample   = hf_max_idx * 2u;
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
