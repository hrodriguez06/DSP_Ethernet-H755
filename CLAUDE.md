# Contexto del repositorio: 3_DSP_Ethernet

Contenedor de proyectos de procesamiento digital de señales (DSP) y telemetría Ethernet sobre STM32H755ZI-Q. Cada proyecto es **autocontenido**: código, proyecto de CubeIDE y herramientas propias, compila sin depender de los demás. Lo único compartido dentro del repositorio es `Docs/`; las hojas de datos viven fuera, en `../0_References/`.

Antes de tocar nada, lee **[`Docs/ARQUITECTURA_DSP_ETH.md`](Docs/ARQUITECTURA_DSP_ETH.md)** y las guías de los proyectos hermanos:
- `../1_ADC/Docs/ADC_EN_EL_H755.md` (calibración VREFINT, trampas del HAL y ADC).
- `../2_Ethernet/Docs/INTEGRACION.md` (reloj de 400 MHz, `FLASH_LATENCY_4`, MPU, descriptores Ethernet en SRAM3).

---

## Invariantes del Sistema Dual-Core

1. **El Cortex-M4 NO escribe registros globales del RCC.**
   Solo escribe en los registros dedicados a su núcleo: `RCC_C2->*ENR` (`AHB1ENR`, `AHB4ENR`, `APB1LENR`).
   Comprobación obligatoria antes de validar:
   ```bash
   grep -rn 'RCC[-]>' CM4_App/
   ```
   Debe retornar vacío.

2. **La SRAM4 (`0x38000000`, 64 KB) es el contrato IPC y buffer Ping-Pong.**
   - Ambos núcleos la ven con la misma dirección física.
   - En el M7, **DEBE ser configurada como NO CACHEABLE en la MPU** (`MPU_ACCESS_NOT_CACHEABLE`) antes de activar el D-Cache (`SCB_EnableDCache()`). De lo contrario, el M7 leerá datos obsoletos del buffer sin fallar.
   - En el M4, no hay D-Cache, pero sus escrituras pasan por un write-buffer: usar `__DSB()` antes de alternar flags de bloque listo.

3. **El M7 es el dueño del arranque y los relojes globales.**
   - Configura PLL1 a 400 MHz con `FLASH_LATENCY_4`.
   - Inicializa Ethernet RMII en GPIOA, GPIOB, GPIOC, GPIOG.
   - Escribe los parámetros de arranque y calibración en SRAM4 y sella el contrato con `magic = DSP_LINK_MAGIC` tras un `__DSB()`.
   - El M4 arranca esperando `magic` para no colisionar con la inicialización de pines compartidos (`GPIOA->MODER`).

4. **El Cortex-M4 es el motor de tiempo real estricto.**
   - Muestreo periódico mediante `TIM3 TRGO` hacia `ADC1`.
   - Mantiene la cadencia exacta $f_s = 25.6\text{ kHz}$ con cero jitter.
   - Llena alternativamente `raw_buffer[0]` y `raw_buffer[1]` ($2 \times 1024$ muestras `uint16_t` = 4 KB en SRAM4).

5. **El Cortex-M7 es el motor DSP y de conectividad.**
   - Convierte $1024$ muestras en crudo LSB a milivoltios/voltios usando la calibración real de $V_{\text{DDA}}$ derivada de $V_{\text{REFINT}}$.
   - Aplica ventana Hanning para mitigar dispersión espectral (*spectral leakage*).
   - Ejecuta `arm_rfft_fast_f32` (CMSIS-DSP) y `arm_cmplx_mag_f32`.
   - Extrae piso de ruido, fundamental $f_0$, hasta 5 armónicos y THD.
   - Emite telemetría por UDP en el puerto `5002` mediante la pila lwIP RAW.

---

## Convenciones de Código

- Comentarios y documentación en **español**, sin acentos dentro del código fuente C.
- Tipos de datos en el contrato inter-core (`dsp_link.h`): usar exclusivamente enteros de tamaño explícito (`uint32_t`, `int32_t`, `uint16_t`) sin estructuras empaquetadas dependientes del compilador.
- Comprobación de colocación en memoria: verificar en el `.map` que la SRAM4 (`0x38000000`) no tenga secciones del linker asignadas por error.
