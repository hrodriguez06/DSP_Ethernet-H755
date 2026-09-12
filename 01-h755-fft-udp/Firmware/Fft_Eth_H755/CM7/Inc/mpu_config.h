/**
  ******************************************************************************
  * @file    mpu_config.h
  * @brief   Configuracion de MPU para Ethernet y SRAM4 en STM32H755 (CM7).
  ******************************************************************************
  */
#ifndef MPU_CONFIG_H
#define MPU_CONFIG_H

/**
  * @brief  Configura SRAM3 (descriptores y pool Ethernet) y SRAM4 (contrato
  *         y buffer Ping-Pong DSP) como no cacheables antes de SCB_EnableDCache().
  */
void APP_MPU_Config(void);

#endif /* MPU_CONFIG_H */
