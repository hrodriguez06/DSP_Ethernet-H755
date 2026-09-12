/**
  ******************************************************************************
  * @file    bm_fault.c
  * @brief   Manejador de HardFault en Cortex-M4 con reporte a SRAM4.
  ******************************************************************************
  */

#include "stm32h7xx.h"
#include "dsp_link.h"

void bm_fault_report(uint32_t *sp)
{
  DSP_LINK->fault_cfsr = SCB->CFSR;
  DSP_LINK->fault_hfsr = SCB->HFSR;
  DSP_LINK->fault_bfar = SCB->BFAR;
  DSP_LINK->fault_pc   = sp[6];
  DSP_LINK->fault_lr   = sp[5];

  __DSB();
  DSP_LINK->fault_flag = DSP_FAULT_MAGIC;
  __DSB();

  for (;;)
  {
    __WFI();
  }
}

__attribute__((naked)) void HardFault_Handler(void)
{
  __asm volatile (
    "  tst   lr, #4              \n"
    "  ite   eq                  \n"
    "  mrseq r0, msp             \n"
    "  mrsne r0, psp             \n"
    "  b     bm_fault_report     \n"
  );
}
