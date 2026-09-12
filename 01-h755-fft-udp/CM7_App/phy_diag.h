/**
  ******************************************************************************
  * @file    phy_diag.h
  * @brief   Diagnostico del PHY LAN8742A (U15) por MDIO.
  ******************************************************************************
  */
#ifndef PHY_DIAG_H
#define PHY_DIAG_H

#include <stdint.h>

#define PHY_REG_BCR      0x00u
#define PHY_REG_BSR      0x01u
#define PHY_REG_ID1      0x02u
#define PHY_REG_ID2      0x03u
#define PHY_REG_ANAR     0x04u
#define PHY_REG_ANLPAR   0x05u
#define PHY_REG_PHYSCSR  0x1Fu

#define PHY_BSR_LINK_UP      (1u << 2)
#define PHY_BSR_AN_COMPLETE  (1u << 5)

int PHY_Diag_Scan(void);
int PHY_Diag_GetAddress(void);
uint32_t PHY_Diag_GetId(void);
int PHY_Diag_GetLinkMode(uint32_t *mbps, uint8_t *full_duplex);
void PHY_Diag_PrintLinkMode(void);
void PHY_Diag_PrintInfo(void);

#endif /* PHY_DIAG_H */
