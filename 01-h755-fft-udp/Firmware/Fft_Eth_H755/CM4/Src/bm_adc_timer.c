/**
  ******************************************************************************
  * @file    bm_adc_timer.c
  * @brief   Adquisicion periodica determinista en Cortex-M4: ADC1 disparado
  *          por TIM3 TRGO en modo Ping-Pong hacia SRAM4.
  ******************************************************************************
  */

#include "bm_adc_timer.h"
#include "bm_rcc.h"

#define TIMEOUT_LOOPS   2000000u

static void delay_us(uint32_t us)
{
  volatile uint32_t n = us * 25u;
  while (n != 0u)
  {
    n--;
  }
}

static int wait_clear(volatile uint32_t *reg, uint32_t mask)
{
  uint32_t n = TIMEOUT_LOOPS;
  while ((*reg & mask) != 0u)
  {
    if (--n == 0u) return -1;
  }
  return 0;
}

static int wait_set(volatile uint32_t *reg, uint32_t mask)
{
  uint32_t n = TIMEOUT_LOOPS;
  while ((*reg & mask) == 0u)
  {
    if (--n == 0u) return -1;
  }
  return 0;
}

static void adc_gpio_init(void)
{
  /* Reloj de GPIOA en el registro del nucleo C2 */
  RCC_C2->AHB4ENR |= RCC_AHB4ENR_GPIOAEN;
  (void)RCC_C2->AHB4ENR;

  /* PA3 en modo analogico (0b11). Read-modify-write para no pisar pines RMII en PA1/PA2/PA7 */
  MODIFY_REG(GPIOA->MODER, GPIO_MODER_MODE3, (0x3UL << GPIO_MODER_MODE3_Pos));
  MODIFY_REG(GPIOA->PUPDR, GPIO_PUPDR_PUPD3, 0u);
}

static int adc1_power_up(void)
{
  ADC1->CR &= ~ADC_CR_DEEPPWD;
  ADC1->CR |= ADC_CR_ADVREGEN;

  delay_us(25u); /* tADCVREG_STUP */

  if (wait_set(&ADC1->ISR, ADC_ISR_LDORDY) != 0)
  {
    return -1;
  }

  /* BOOST segun kernel ck (0b10 para rango 12.5 a 25 MHz) */
  MODIFY_REG(ADC1->CR, ADC_CR_BOOST, (0x2UL << ADC_CR_BOOST_Pos));
  return 0;
}

static int adc1_calibrate(void)
{
  if ((ADC1->CR & ADC_CR_ADEN) != 0u)
  {
    return -1;
  }

  ADC1->CR &= ~ADC_CR_ADCALDIF; /* Single-ended */
  ADC1->CR |= ADC_CR_ADCALLIN;  /* Calibracion lineal */
  ADC1->CR |= ADC_CR_ADCAL;

  return wait_clear(&ADC1->CR, ADC_CR_ADCAL);
}

static int adc1_enable(void)
{
  ADC1->ISR = ADC_ISR_ADRDY;
  ADC1->CR |= ADC_CR_ADEN;

  if (wait_set(&ADC1->ISR, ADC_ISR_ADRDY) != 0)
  {
    return -1;
  }

  ADC1->ISR = ADC_ISR_ADRDY;
  return 0;
}

static void dma1_stream0_init(void)
{
  /* 1. Deshabilitar el stream 0 por si estuviera activo y esperar a que EN = 0 */
  DMA1_Stream0->CR &= ~DMA_SxCR_EN;
  while ((DMA1_Stream0->CR & DMA_SxCR_EN) != 0u)
  {
  }

  /* 2. Limpiar todos los flags de interrupcion del Stream 0 en LIFCR */
  DMA1->LIFCR = DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0 |
                DMA_LIFCR_CTEIF0 | DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CFEIF0;

  /* 3. Asignar la solicitud ADC1 (ID = 9) al canal 0 de DMAMUX1 (Stream 0 de DMA1) */
  DMAMUX1_Channel0->CCR = (9U << DMAMUX_CxCR_DMAREQ_ID_Pos);

  /* 4. Direccion periferica: registro de datos de conversion del ADC1 */
  DMA1_Stream0->PAR = (uint32_t)&(ADC1->DR);

  /* 5. Direccion de memoria base: inicio del buffer Ping-Pong en SRAM4 */
  DMA1_Stream0->M0AR = (uint32_t)&(DSP_LINK->raw_buffer[0][0]);

  /* 6. Cantidad de elementos: 2 bloques de 4096 muestras (8192 medias palabras) */
  DMA1_Stream0->NDTR = 2U * DSP_BLOCK_SIZE;

  /* 7. FIFO Control: modo directo sin FIFO (DMDIS = 0) */
  DMA1_Stream0->FCR = 0u;

  /* 8. Configuracion del stream:
   *    - Prioridad: Muy Alta (PL = 0b11)
   *    - MSIZE = 16 bits (0b01)
   *    - PSIZE = 16 bits (0b01)
   *    - MINC = Incremento de memoria activado
   *    - PINC = Incremento de periferico desactivado (fijo en ADC1->DR)
   *    - CIRC = Modo circular continuo activado
   *    - DIR = Periferico a Memoria (0b00)
   *    - HTIE = Interrupcion de media transferencia (buffer 0 completo)
   *    - TCIE = Interrupcion de transferencia completa (buffer 1 completo)
   *    - TEIE = Interrupcion de error de bus
   */
  DMA1_Stream0->CR = (0x3UL << DMA_SxCR_PL_Pos)    |
                     (0x1UL << DMA_SxCR_MSIZE_Pos) |
                     (0x1UL << DMA_SxCR_PSIZE_Pos) |
                     DMA_SxCR_MINC                 |
                     DMA_SxCR_CIRC                 |
                     DMA_SxCR_HTIE                 |
                     DMA_SxCR_TCIE                 |
                     DMA_SxCR_TEIE;

  /* 9. Configurar NVIC para DMA1_Stream0_IRQn (prioridad 2) */
  NVIC_SetPriority(DMA1_Stream0_IRQn, 2);
  NVIC_EnableIRQ(DMA1_Stream0_IRQn);
}

int BM_ADC_Timer_Init(uint32_t timer_clk_hz, uint32_t fs_hz)
{
  if (fs_hz == 0u)
  {
    fs_hz = DSP_FS_DEFAULT;
  }

  /* 1. Habilitar relojes de perifericos en el Cortex-M4: ADC1/2, DMA1 y TIM3 */
  RCC_C2->AHB1ENR |= RCC_AHB1ENR_ADC12EN | RCC_AHB1ENR_DMA1EN;
  RCC_C2->APB1LENR |= RCC_APB1LENR_TIM3EN;
  (void)RCC_C2->AHB1ENR;
  (void)RCC_C2->APB1LENR;

  /* 2. Inicializar GPIO PA3 */
  adc_gpio_init();

  /* 3. Inicializar DMA1 Stream 0 y DMAMUX1 Canal 0 */
  dma1_stream0_init();

  /* 4. Prescaler comun ADC1/2: /4 (0b0010 = 0x2) */
  ADC12_COMMON->CCR = (0x2UL << ADC_CCR_PRESC_Pos);

  /* 5. Encendido, calibracion y habilitacion del ADC1 */
  if (adc1_power_up() != 0) return -1;
  if (adc1_calibrate() != 0) return -2;

  /* Preseleccion del canal 15 (PA3 / A0): DEBE configurarse con ADEN=0.
     Sin esto, el conmutador analogico de la E/S esta abierto y el pin no se conecta. */
  ADC1->PCSEL |= (1UL << 15);

  if (adc1_enable() != 0) return -3;

  /* 6. Configuracion del canal 15 (PA3):
        SMPR2 cubre canales 10 a 19. SMP15 = 2 (8.5 ciclos de conversion) */
  MODIFY_REG(ADC1->SMPR2, ADC_SMPR2_SMP15, (0x2UL << ADC_SMPR2_SMP15_Pos));

  /* Secuencia regular: 1 conversion, canal 15 en SQ1 */
  ADC1->SQR1 = (15UL << ADC_SQR1_SQ1_Pos);

  /* Disparo externo por hardware: EXTSEL=4 (TIM3 TRGO), EXTEN=1 (flanco de subida)
     y gestion de datos por DMA en modo circular: DMNGT=3 */
  ADC1->CFGR = (4UL << ADC_CFGR_EXTSEL_Pos) |
               (1UL << ADC_CFGR_EXTEN_Pos)  |
               (3UL << ADC_CFGR_DMNGT_Pos);

  /* Deshabilitar interrupcion EOC del ADC (el DMA gestiona la transferencia) */
  ADC1->IER = 0u;

  /* 7. Configuracion de TIM3 como generador de TRGO */
  BM_ADC_Timer_SetFs(timer_clk_hz, fs_hz);

  return 0;
}

void BM_ADC_Timer_SetFs(uint32_t timer_clk_hz, uint32_t fs_hz)
{
  if (fs_hz == 0u) fs_hz = DSP_FS_DEFAULT;
  if (timer_clk_hz == 0u) timer_clk_hz = 200000000u;

  uint32_t total_div = timer_clk_hz / fs_hz;
  if (total_div < 2u) total_div = 2u;

  TIM3->CR1 = 0u;
  TIM3->CR2 = (0x2UL << TIM_CR2_MMS_Pos); /* MMS = 010 (Update event -> TRGO) */
  TIM3->PSC = 0u;
  TIM3->ARR = total_div - 1u;
  TIM3->EGR = TIM_EGR_UG;
}

void BM_ADC_Timer_Start(void)
{
  /* Asegurar que el stream DMA este habilitado antes de arrancar el ADC */
  if ((DMA1_Stream0->CR & DMA_SxCR_EN) == 0u)
  {
    DMA1->LIFCR = DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0 |
                  DMA_LIFCR_CTEIF0 | DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CFEIF0;
    DMA1_Stream0->NDTR = 2U * DSP_BLOCK_SIZE;
    DMA1_Stream0->CR |= DMA_SxCR_EN;
  }

  ADC1->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;
  ADC1->CR |= ADC_CR_ADSTART;
  TIM3->CR1 |= TIM_CR1_CEN;
  DSP_LINK->flags |= DSP_FLAG_RUNNING;
  __DSB();
}

void BM_ADC_Timer_Stop(void)
{
  TIM3->CR1 &= ~TIM_CR1_CEN;
  ADC1->CR |= ADC_CR_ADSTP;
  (void)wait_clear(&ADC1->CR, ADC_CR_ADSTP);

  DMA1_Stream0->CR &= ~DMA_SxCR_EN;
  (void)wait_clear(&DMA1_Stream0->CR, DMA_SxCR_EN);

  DSP_LINK->flags &= ~DSP_FLAG_RUNNING;
  __DSB();
}

/**
  * @brief  Manejador de interrupcion del DMA1 Stream 0.
  *         Se ejecuta en dos eventos por ciclo de 8192 muestras (cadencia de 6.25 Hz):
  *         - HT (Half Transfer): Buffer 0 lleno (4096 muestras).
  *         - TC (Transfer Complete): Buffer 1 lleno (4096 muestras).
  *         Alterna indices de Ping-Pong y notifica al Cortex-M7 con minima latencia.
  */
void DMA1_Stream0_IRQHandler(void)
{
  uint32_t lisr = DMA1->LISR;

  if ((lisr & DMA_LISR_HTIF0) != 0u)
  {
    /* Limpiar flag de Half Transfer */
    DMA1->LIFCR = DMA_LIFCR_CHTIF0;

    /* Buffer 0 (primeras 4096 muestras) esta completo y listo para CM7 */
    DSP_LINK->ready_idx    = 0u;
    DSP_LINK->active_idx   = 1u;
    DSP_LINK->sample_count = DSP_BLOCK_SIZE;
    DSP_LINK->block_id++;
    DSP_LINK->t_block_ms   = BM_GetTick();
    __DSB();
  }

  if ((lisr & DMA_LISR_TCIF0) != 0u)
  {
    /* Limpiar flag de Transfer Complete */
    DMA1->LIFCR = DMA_LIFCR_CTCIF0;

    /* Buffer 1 (segundas 4096 muestras) esta completo y listo para CM7 */
    DSP_LINK->ready_idx    = 1u;
    DSP_LINK->active_idx   = 0u;
    DSP_LINK->sample_count = DSP_BLOCK_SIZE;
    DSP_LINK->block_id++;
    DSP_LINK->t_block_ms   = BM_GetTick();
    __DSB();
  }

  if ((lisr & DMA_LISR_TEIF0) != 0u)
  {
    /* Limpiar flag de Transfer Error */
    DMA1->LIFCR = DMA_LIFCR_CTEIF0;
  }
}

/**
  * @brief  Fallback por si el ADC genera una interrupcion inesperada.
  */
void ADC_IRQHandler(void)
{
  ADC1->ISR = ADC1->ISR;
}

