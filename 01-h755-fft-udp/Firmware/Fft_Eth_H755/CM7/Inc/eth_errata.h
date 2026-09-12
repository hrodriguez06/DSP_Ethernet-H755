/**
  ******************************************************************************
  * @file    eth_errata.h
  * @brief   Workarounds de la errata ES0445 para el Ethernet del STM32H755.
  ******************************************************************************
  */
#ifndef ETH_ERRATA_H
#define ETH_ERRATA_H

void ETH_Errata_ApplyAtInit(void);

#endif /* ETH_ERRATA_H */
