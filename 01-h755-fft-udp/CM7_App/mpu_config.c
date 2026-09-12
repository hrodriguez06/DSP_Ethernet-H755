/**
  ******************************************************************************
  * @file    mpu_config.c
  * @brief   Configuracion de MPU para Ethernet y SRAM4 en STM32H755 (CM7).
  *
  *  Regiones programadas:
  *    Region 0: 0x30040000 (32 KB, SRAM3) -> Normal no cacheable (pool lwIP y heap)
  *    Region 1: 0x30040000 (1 KB)          -> Device (descriptores DMA Ethernet)
  *    Region 2: 0x38000000 (64 KB, SRAM4) -> Normal no cacheable (DSP_LINK y Ping-Pong)
  ******************************************************************************
  */

#include "mpu_config.h"
#include "main.h"
#include "dsp_link.h"

void APP_MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  HAL_MPU_Disable();

  /* --- Region 0: toda la SRAM3, Normal no cacheable (Pool lwIP y Heap) --- */
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress      = 0x30040000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_32KB;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* --- Region 1: descriptores DMA Ethernet, tipo Device --- */
  MPU_InitStruct.Number           = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress      = 0x30040000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_1KB;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_BUFFERABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* --- Region 2: SRAM4 (0x38000000, 64 KB), Normal no cacheable --- */
  MPU_InitStruct.Number           = MPU_REGION_NUMBER2;
  MPU_InitStruct.BaseAddress      = DSP_LINK_BASE;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_64KB;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}
