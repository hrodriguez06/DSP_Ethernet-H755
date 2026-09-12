/**
  ******************************************************************************
  * @file    app_ethernet.c
  * @brief   Supervisor de red: enlace, DHCP e indicacion por LEDs para CM7.
  ******************************************************************************
  */

#include "app_ethernet.h"
#include "udp_dsp.h"
#include "phy_diag.h"
#include "board_io.h"
#include "main.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/ip_addr.h"
#include <stdio.h>

extern struct netif gnetif;

#define BLINK_PERIOD_MS   400u

static AppNetState_t s_state;
static uint32_t      s_dhcp_start_ms;
static uint32_t      s_blink_ms;
static bool          s_blink_on;
static int           s_udp_started = 0;

static void led_link(bool on)  { Board_IO_SetLed(1, on ? 1 : 0); }
static void led_error(bool on) { Board_IO_SetLed(3, on ? 1 : 0); }

static void print_addresses(void)
{
  printf("  IP      : %lu.%lu.%lu.%lu\r\n",
         (unsigned long)ip4_addr1_16(netif_ip4_addr(&gnetif)),
         (unsigned long)ip4_addr2_16(netif_ip4_addr(&gnetif)),
         (unsigned long)ip4_addr3_16(netif_ip4_addr(&gnetif)),
         (unsigned long)ip4_addr4_16(netif_ip4_addr(&gnetif)));
  printf("  Mascara : %lu.%lu.%lu.%lu\r\n",
         (unsigned long)ip4_addr1_16(netif_ip4_netmask(&gnetif)),
         (unsigned long)ip4_addr2_16(netif_ip4_netmask(&gnetif)),
         (unsigned long)ip4_addr3_16(netif_ip4_netmask(&gnetif)),
         (unsigned long)ip4_addr4_16(netif_ip4_netmask(&gnetif)));
  printf("  Gateway : %lu.%lu.%lu.%lu\r\n",
         (unsigned long)ip4_addr1_16(netif_ip4_gw(&gnetif)),
         (unsigned long)ip4_addr2_16(netif_ip4_gw(&gnetif)),
         (unsigned long)ip4_addr3_16(netif_ip4_gw(&gnetif)),
         (unsigned long)ip4_addr4_16(netif_ip4_gw(&gnetif)));
}

static void apply_fallback_address(void)
{
  ip4_addr_t ip, mask, gw;
  IP4_ADDR(&ip,   APP_FALLBACK_IP0,   APP_FALLBACK_IP1,   APP_FALLBACK_IP2,   APP_FALLBACK_IP3);
  IP4_ADDR(&mask, APP_FALLBACK_MASK0, APP_FALLBACK_MASK1, APP_FALLBACK_MASK2, APP_FALLBACK_MASK3);
  IP4_ADDR(&gw,   APP_FALLBACK_GW0,   APP_FALLBACK_GW1,   APP_FALLBACK_GW2,   APP_FALLBACK_GW3);
  netif_set_addr(&gnetif, &ip, &mask, &gw);
}

static void enter_link_down(void)
{
  s_state = APP_NET_LINK_DOWN;
  led_link(false);
  led_error(true);
  printf("\r\n[NET] Enlace caido. Revisa el cable RJ45.\r\n");
}

static void enter_dhcp_wait(void)
{
  s_state         = APP_NET_DHCP_WAIT;
  s_dhcp_start_ms = HAL_GetTick();
  led_error(false);

  printf("\r\n[NET] Enlace arriba.\r\n");
  PHY_Diag_PrintLinkMode();

  netif_set_addr(&gnetif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
  dhcp_start(&gnetif);
  printf("[NET] Solicitando direccion por DHCP (limite %lu s)...\r\n",
         (unsigned long)(APP_DHCP_TIMEOUT_MS / 1000u));
}

void APP_Ethernet_Init(void)
{
  s_state    = APP_NET_LINK_DOWN;
  s_blink_ms = HAL_GetTick();
  s_blink_on = false;
  s_udp_started = 0;

  led_link(false);
  led_error(true);

  dhcp_stop(&gnetif);
  netif_set_addr(&gnetif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
}

void APP_Ethernet_Process(void)
{
  const bool     link = netif_is_link_up(&gnetif);
  const uint32_t now  = HAL_GetTick();

  if (!link && s_state != APP_NET_LINK_DOWN)
  {
    dhcp_stop(&gnetif);
    enter_link_down();
  }
  else if (link && s_state == APP_NET_LINK_DOWN)
  {
    enter_dhcp_wait();
  }

  if (s_state == APP_NET_DHCP_WAIT)
  {
    if (dhcp_supplied_address(&gnetif))
    {
      s_state = APP_NET_READY_DHCP;
      led_link(true);
      led_error(false);
      printf("\r\n[NET] DHCP concedido:\r\n");
      print_addresses();
      if (!s_udp_started)
      {
        UDP_DSP_Init();
        s_udp_started = 1;
        printf("[DSP] Servidor UDP listo en puerto %u\r\n", UDP_DSP_PORT);
      }
    }
    else if ((now - s_dhcp_start_ms) >= APP_DHCP_TIMEOUT_MS)
    {
      dhcp_stop(&gnetif);
      apply_fallback_address();
      s_state = APP_NET_READY_STATIC;
      led_link(true);
      printf("\r\n[NET] Sin respuesta DHCP; aplicando IP fija de respaldo:\r\n");
      print_addresses();
      if (!s_udp_started)
      {
        UDP_DSP_Init();
        s_udp_started = 1;
        printf("[DSP] Servidor UDP listo en puerto %u\r\n", UDP_DSP_PORT);
      }
    }
  }

  /* Parpadeo indicativo segun estado */
  if ((now - s_blink_ms) >= BLINK_PERIOD_MS)
  {
    s_blink_ms = now;
    s_blink_on = !s_blink_on;

    if (s_state == APP_NET_DHCP_WAIT)
    {
      led_link(s_blink_on);
    }
    else if (s_state == APP_NET_READY_STATIC)
    {
      led_error(s_blink_on);
    }
  }
}

AppNetState_t APP_Ethernet_GetState(void)
{
  return s_state;
}

const char *APP_Ethernet_GetStateName(void)
{
  switch (s_state)
  {
    case APP_NET_LINK_DOWN:    return "LINK_DOWN";
    case APP_NET_DHCP_WAIT:    return "DHCP_WAIT";
    case APP_NET_READY_DHCP:   return "READY_DHCP";
    case APP_NET_READY_STATIC: return "READY_STATIC";
    default:                   return "UNKNOWN";
  }
}

void APP_Ethernet_PrintStatus(void)
{
  printf("\r\n===== Estado de Red =====\r\n");
  printf("  Estado  : %s\r\n", APP_Ethernet_GetStateName());
  if (s_state == APP_NET_READY_DHCP || s_state == APP_NET_READY_STATIC)
  {
    print_addresses();
  }
  printf("=========================\r\n");
}
