/**
  ******************************************************************************
  * @file    app_ethernet.h
  * @brief   Supervisor de red: enlace, DHCP e indicacion por LEDs.
  ******************************************************************************
  */
#ifndef APP_ETHERNET_H
#define APP_ETHERNET_H

#include <stdint.h>
#include <stdbool.h>

#define APP_FALLBACK_IP0    192
#define APP_FALLBACK_IP1    168
#define APP_FALLBACK_IP2      1
#define APP_FALLBACK_IP3     50

#define APP_FALLBACK_MASK0  255
#define APP_FALLBACK_MASK1  255
#define APP_FALLBACK_MASK2  255
#define APP_FALLBACK_MASK3    0

#define APP_FALLBACK_GW0    192
#define APP_FALLBACK_GW1    168
#define APP_FALLBACK_GW2      1
#define APP_FALLBACK_GW3      1

#define APP_DHCP_TIMEOUT_MS  12000u

typedef enum
{
  APP_NET_LINK_DOWN = 0,
  APP_NET_DHCP_WAIT,
  APP_NET_READY_DHCP,
  APP_NET_READY_STATIC
} AppNetState_t;

void          APP_Ethernet_Init(void);
void          APP_Ethernet_Process(void);
AppNetState_t APP_Ethernet_GetState(void);
const char   *APP_Ethernet_GetStateName(void);
void          APP_Ethernet_PrintStatus(void);

#endif /* APP_ETHERNET_H */
