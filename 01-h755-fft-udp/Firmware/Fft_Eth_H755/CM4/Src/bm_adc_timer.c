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

int BM_ADC_Timer_Init(uint32_t timer_clk_hz, uint32_t fs_hz)
{
  if (fs_hz == 0u)
  {
    fs_hz = DSP_FS_DEFAULT;
  }

  /* 1. Habilitar relojes de perifericos en el Cortex-M4 */
  RCC_C2->AHB1ENR |= RCC_AHB1ENR_ADC12EN;
  RCC_C2->APB1LENR |= RCC_APB1LENR_TIM3EN;
  (void)RCC_C2->AHB1ENR;
  (void)RCC_C2->APB1LENR;

  /* 2. Inicializar GPIO PA3 */
  adc_gpio_init();

  /* 3. Prescaler comun ADC1/2: /4 (0b0010 = 0x2) */
  ADC12_COMMON->CCR = (0x2UL << ADC_CCR_PRESC_Pos);

  /* 4. Encendido, calibracion y habilitacion del ADC1 */
  if (adc1_power_up() != 0) return -1;
  if (adc1_calibrate() != 0) return -2;

  /* Preseleccion del canal 15 (PA3 / A0): DEBE configurarse con ADEN=0.
     Sin esto, el conmutador analogico de la E/S esta abierto y el pin no se conecta. */
  ADC1->PCSEL |= (1UL << 15);

  if (adc1_enable() != 0) return -3;

  /* 5. Configuracion del canal 15 (PA3):
        SMPR2 cubre canales 10 a 19. SMP15 = 2 (8.5 ciclos de conversion) */
  MODIFY_REG(ADC1->SMPR2, ADC_SMPR2_SMP15, (0x2UL << ADC_SMPR2_SMP15_Pos));

  /* Secuencia regular: 1 conversion, canal 15 en SQ1 */
  ADC1->SQR1 = (15UL << ADC_SQR1_SQ1_Pos);

  /* Disparo externo por hardware: EXTSEL=4 (TIM3 TRGO), EXTEN=1 (flanco de subida) */
  ADC1->CFGR = (4UL << ADC_CFGR_EXTSEL_Pos) | (1UL << ADC_CFGR_EXTEN_Pos);

  /* Habilitar interrupcion EOC */
  ADC1->IER |= ADC_IER_EOCIE;

  /* 6. Configuracion de TIM3 como generador de TRGO */
  BM_ADC_Timer_SetFs(timer_clk_hz, fs_hz);

  /* Configurar NVIC para ADC_IRQn (prioridad 2) */
  NVIC_SetPriority(ADC_IRQn, 2);
  NVIC_EnableIRQ(ADC_IRQn);

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
  DSP_LINK->flags &= ~DSP_FLAG_RUNNING;
  __DSB();
}

/**
  * @brief  Manejador de interrupcion de conversion ADC1.
  *         Se ejecuta a la cadencia exacta de fs (ej. 25.6 kHz, cada 39 us).
  *         Inserta la muestra en el buffer Ping-Pong activo en SRAM4.
  */
void ADC_IRQHandler(void)
{
  /* Leer dato (limpia automaticamente el flag EOC) */
  uint16_t sample = (uint16_t)(ADC1->DR & 0xFFFFu);

  uint32_t act   = DSP_LINK->active_idx;
  uint32_t count = DSP_LINK->sample_count;

  /* Limpiar flag de clipping al inicio de cada nuevo bloque */
  if (count == 0u)
  {
    DSP_LINK->flags &= ~DSP_FLAG_CLIPPING;
  }

  /* Chequeo de saturacion en rieles (cerca de 0 V o 3.3 V) */
  if ((sample <= 2u) || (sample >= 0xFFFDu))
  {
    DSP_LINK->flags |= DSP_FLAG_CLIPPING;
  }

  DSP_LINK->raw_buffer[act][count] = sample;
  count++;

  if (count >= DSP_BLOCK_SIZE)
  {
    /* Bloque completo: conmutar Ping-Pong y notificar al Cortex-M7 */
    DSP_LINK->ready_idx  = act;
    act ^= 1u;
    DSP_LINK->active_idx = act;
    DSP_LINK->sample_count = 0u;
    DSP_LINK->block_id++;
    DSP_LINK->t_block_ms = BM_GetTick();
    __DSB();
  }
  else
  {
    DSP_LINK->sample_count = count;
  }
}
