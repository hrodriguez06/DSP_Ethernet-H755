/**
  ******************************************************************************
  * @file    udp_dsp.c
  * @brief   Servidor y emisor UDP de telemetria espectral (puerto 5002) en CM7.
  ******************************************************************************
  */

#include "udp_dsp.h"
#include "dsp_link.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static struct udp_pcb *s_pcb;
static ip_addr_t s_sub_addr;
static u16_t     s_sub_port = 0;

static uint32_t s_tx_packets = 0;
static uint32_t s_tx_errors  = 0;

static void send_raw(const ip_addr_t *addr, u16_t port, const void *data, uint16_t len)
{
  if (s_pcb == NULL || port == 0 || len == 0) return;

  struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
  if (p == NULL)
  {
    s_tx_errors++;
    return;
  }

  memcpy(p->payload, data, len);
  if (udp_sendto(s_pcb, p, addr, port) == ERR_OK)
  {
    s_tx_packets++;
  }
  else
  {
    s_tx_errors++;
  }
  pbuf_free(p);
}

static void send_text(const ip_addr_t *addr, u16_t port, const char *msg)
{
  send_raw(addr, port, msg, (uint16_t)strlen(msg));
}

int UDP_DSP_HasSubscribers(void)
{
  return (s_sub_port != 0);
}

void UDP_DSP_SendTelemetry(const DSP_AnalysisResult_t *result,
                           uint32_t block_id,
                           uint32_t timestamp_ms)
{
  if (s_sub_port == 0) return;

  UDP_DSP_TelemetryPacket_t pkt;
  pkt.magic           = UDP_DSP_MAGIC_TELEMETRY;
  pkt.block_id        = block_id;
  pkt.timestamp_ms    = timestamp_ms;
  pkt.fs_hz           = DSP_LINK->fs_hz;
  pkt.n_fft           = DSP_BLOCK_SIZE;
  pkt.f0_hz_x10       = (uint32_t)(result->f0_hz * 10.0f);
  pkt.f0_mag_mv       = (uint32_t)result->f0_mag_mv;
  pkt.noise_floor_mv  = (uint32_t)result->noise_floor_mv;
  pkt.snr_db_x10      = (uint32_t)(result->snr_db * 10.0f);
  pkt.thd_pct_x10     = (uint32_t)(result->thd_pct * 10.0f);
  pkt.flags           = DSP_LINK->flags;

  for (int i = 0; i < 10; i++)
  {
    pkt.harm_mag_mv[i] = (uint16_t)result->harm_mag_mv[i];
  }
  pkt.dc_offset_mv = (uint16_t)result->dc_offset_mv;
  pkt.v_pp_mv      = (uint16_t)result->v_pp_mv;
  pkt.rsv[0] = 0;

  send_raw(&s_sub_addr, s_sub_port, &pkt, sizeof(pkt));
}

void UDP_DSP_SendFullSpectrum(const float *mag_spectrum, uint32_t block_id)
{
  if (s_sub_port == 0) return;

  /* Paquete de espectro: cabecera (12 B) + 512 magnitudes uint16_t en mV (1024 B) = 1036 B */
  struct __attribute__((packed)) {
    uint32_t magic;       /* UDP_DSP_MAGIC_SPECTRUM: 0x53504543 */
    uint32_t block_id;
    uint16_t part;        /* 0 = bins 0..511, 1 = bins 512..1023 */
    uint16_t total_bins;  /* 1024 */
    uint16_t bins[512];
  } spec_pkt;

  spec_pkt.magic      = UDP_DSP_MAGIC_SPECTRUM;
  spec_pkt.block_id   = block_id;
  spec_pkt.total_bins = DSP_NUM_BINS;

  /* Parte 0: Bins 0 .. 511 (0 a 6.4 kHz) */
  spec_pkt.part = 0;
  for (uint32_t k = 0; k < 512; k++)
  {
    float val = mag_spectrum[k];
    if (val > 65535.0f) val = 65535.0f;
    spec_pkt.bins[k] = (uint16_t)val;
  }
  send_raw(&s_sub_addr, s_sub_port, &spec_pkt, sizeof(spec_pkt));

  /* Parte 1: Bins 512 .. 1023 (6.4 a 12.8 kHz) */
  spec_pkt.part = 1;
  for (uint32_t k = 0; k < 512; k++)
  {
    float val = mag_spectrum[512 + k];
    if (val > 65535.0f) val = 65535.0f;
    spec_pkt.bins[k] = (uint16_t)val;
  }
  send_raw(&s_sub_addr, s_sub_port, &spec_pkt, sizeof(spec_pkt));
}

static void udp_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                              const ip_addr_t *addr, u16_t port)
{
  (void)arg;
  (void)pcb;

  if (p == NULL) return;

  char buf[64];
  uint16_t len = (p->len < (sizeof(buf) - 1)) ? p->len : (sizeof(buf) - 1);
  memcpy(buf, p->payload, len);
  buf[len] = '\0';
  pbuf_free(p);

  /* Trim de caracteres de fin de linea */
  while (len > 0 && (buf[len-1] == '\r' || buf[len-1] == '\n' || buf[len-1] == ' '))
  {
    buf[--len] = '\0';
  }

  for (uint16_t i = 0; i < len; i++)
  {
    buf[i] = (char)toupper((unsigned char)buf[i]);
  }

  if (strcmp(buf, "STREAM") == 0)
  {
    s_sub_addr = *addr;
    s_sub_port = port;
    send_text(addr, port, "OK: Streaming habilitado a 25 Hz\r\n");
  }
  else if (strcmp(buf, "STOP") == 0)
  {
    s_sub_port = 0;
    send_text(addr, port, "OK: Streaming detenido\r\n");
  }
  else if (strcmp(buf, "SPECTRUM") == 0)
  {
    s_sub_addr = *addr;
    s_sub_port = port;
    UDP_DSP_SendFullSpectrum(DSP_Engine_GetMagnitudeSpectrum(), DSP_LINK->block_id);
  }
  else if (strcmp(buf, "STATUS") == 0)
  {
    char resp[128];
    uint32_t f0_x10 = DSP_LINK->f0_hz_x10;
    uint32_t thd_x10 = DSP_LINK->thd_pct_x10;
    snprintf(resp, sizeof(resp),
             "STATUS: block=%lu f0=%lu.%luHz Vpk=%lumV Vdc=%lumV Vpp=%lumV noise=%lumV thd=%lu.%lu%% flags=0x%lx\r\n",
             DSP_LINK->block_id,
             f0_x10 / 10u, f0_x10 % 10u,
             DSP_LINK->f0_mag_mv,
             DSP_LINK->dc_offset_mv,
             DSP_LINK->v_pp_mv,
             DSP_LINK->noise_floor_mv,
             thd_x10 / 10u, thd_x10 % 10u,
             DSP_LINK->flags);
    send_text(addr, port, resp);
  }
  else if (strncmp(buf, "FS ", 3) == 0)
  {
    uint32_t new_fs = (uint32_t)strtoul(&buf[3], NULL, 10);
    if (new_fs >= 1000 && new_fs <= 100000)
    {
      DSP_LINK->cmd_fs_hz = new_fs;
      DSP_LINK->cmd_seq++;
      __DSB();
      send_text(addr, port, "OK: Solicitud de cambio de fs enviada al CM4\r\n");
    }
    else
    {
      send_text(addr, port, "ERR: fs fuera de rango (1000 a 100000 Hz)\r\n");
    }
  }
  else if (strcmp(buf, "HELP") == 0)
  {
    send_text(addr, port, "Comandos: STREAM, STOP, SPECTRUM, STATUS, FS <hz>, HELP\r\n");
  }
  else
  {
    send_text(addr, port, "ERR: Comando no reconocido. Escribe HELP\r\n");
  }
}

void UDP_DSP_Init(void)
{
  s_pcb = udp_new();
  if (s_pcb != NULL)
  {
    err_t err = udp_bind(s_pcb, IP_ADDR_ANY, UDP_DSP_PORT);
    if (err == ERR_OK)
    {
      udp_recv(s_pcb, udp_recv_callback, NULL);
    }
  }
}
