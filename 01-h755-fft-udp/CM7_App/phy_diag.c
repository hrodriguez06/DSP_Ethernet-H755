/**
  ******************************************************************************
  * @file    phy_diag.c
  * @brief   Diagnostico del PHY LAN8742A (U15) por MDIO.
  ******************************************************************************
  */

#include "phy_diag.h"
#include "main.h"
#include <stdio.h>

extern ETH_HandleTypeDef heth;

static int      s_addr = -1;
static uint32_t s_id;

static int phy_read(uint32_t addr, uint32_t reg, uint32_t *val)
{
  return (HAL_ETH_ReadPHYRegister(&heth, addr, reg, val) == HAL_OK) ? 0 : -1;
}

int PHY_Diag_Scan(void)
{
  s_addr = -1;
  s_id   = 0u;

  for (uint32_t a = 0u; a < 32u; a++)
  {
    uint32_t id1 = 0u, id2 = 0u;
    if (phy_read(a, PHY_REG_ID1, &id1) != 0) continue;
    if (phy_read(a, PHY_REG_ID2, &id2) != 0) continue;

    if ((id1 == 0x0000u && id2 == 0x0000u) ||
        (id1 == 0xFFFFu && id2 == 0xFFFFu))
    {
      continue;
    }

    s_addr = (int)a;
    s_id   = ((id1 & 0xFFFFu) << 16) | (id2 & 0xFFFFu);
    return 0;
  }
  return -1;
}

int PHY_Diag_GetAddress(void)
{
  return s_addr;
}

uint32_t PHY_Diag_GetId(void)
{
  return s_id;
}

int PHY_Diag_GetLinkMode(uint32_t *mbps, uint8_t *full_duplex)
{
  uint32_t bsr = 0u, scsr = 0u;

  if (s_addr < 0) return -1;
  if (phy_read((uint32_t)s_addr, PHY_REG_BSR, &bsr) != 0) return -1;

  if ((bsr & PHY_BSR_LINK_UP) == 0u)
  {
    if (phy_read((uint32_t)s_addr, PHY_REG_BSR, &bsr) != 0) return -1;
    if ((bsr & PHY_BSR_LINK_UP) == 0u) return -1;
  }

  if (phy_read((uint32_t)s_addr, PHY_REG_PHYSCSR, &scsr) != 0) return -1;

  switch ((scsr >> 2) & 0x07u)
  {
    case 0x01u: *mbps = 10u;  *full_duplex = 0u; break;
    case 0x05u: *mbps = 10u;  *full_duplex = 1u; break;
    case 0x02u: *mbps = 100u; *full_duplex = 0u; break;
    case 0x06u: *mbps = 100u; *full_duplex = 1u; break;
    default:    return -1;
  }
  return 0;
}

void PHY_Diag_PrintLinkMode(void)
{
  uint32_t mbps;
  uint8_t  fd;

  if (PHY_Diag_GetLinkMode(&mbps, &fd) == 0)
  {
    printf("  Enlace  : %lu Mbps %s\r\n",
           (unsigned long)mbps, fd ? "full-duplex" : "half-duplex");
  }
  else
  {
    printf("  Enlace  : sin establecer\r\n");
  }
}

void PHY_Diag_PrintInfo(void)
{
  printf("\r\n===== PHY LAN8742A =====\r\n");
  if (PHY_Diag_Scan() != 0)
  {
    printf("  No respondio ningun PHY en MDIO 0..31.\r\n");
    printf("========================\r\n");
    return;
  }
  printf("  Direccion MDIO : %d\r\n", s_addr);
  printf("  PHY ID         : 0x%08lX\r\n", (unsigned long)s_id);
  PHY_Diag_PrintLinkMode();
  printf("========================\r\n");
}
