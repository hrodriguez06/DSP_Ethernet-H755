/**
  ******************************************************************************
  * @file    eth_errata.c
  * @brief   Workarounds de la errata ES0445. Ver eth_errata.h.
  ******************************************************************************
  */

#include "eth_errata.h"
#include "main.h"

extern ETH_HandleTypeDef heth;

void ETH_Errata_ApplyAtInit(void)
{
  ETH_DMAConfigTypeDef dmaconf = {0};
  ETH_MACConfigTypeDef macconf = {0};

  /* ES0445 2.25.4: Prioridad fija Rx sobre Tx */
  HAL_ETH_GetDMAConfig(&heth, &dmaconf);
  dmaconf.DMAArbitration = ETH_DMAARBITRATION_RX;
  HAL_ETH_SetDMAConfig(&heth, &dmaconf);

  /* ES0445 2.25.10: Reenvio de paquetes en RMII 10 Mbps */
  HAL_ETH_GetMACConfig(&heth, &macconf);
  macconf.ForwardRxErrorPacket = ENABLE;
  HAL_ETH_SetMACConfig(&heth, &macconf);
}
