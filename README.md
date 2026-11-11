# DSP & Ethernet — I+D

Proyectos de procesamiento digital de señales (DSP) y conectividad Ethernet sobre **STM32H755ZI-Q** (NUCLEO-H755ZI-Q). Cada proyecto es **autocontenido**: lleva su propio código, su proyecto de STM32CubeIDE y sus herramientas, y compila sin depender de los demás. Lo único compartido es la documentación transversal y las hojas de datos.

Este repositorio materializa la **justificación arquitectónica del procesador dual-core**:
1. El **Cortex-M4 (200 MHz, Bare-Metal)** garantiza el **tiempo real duro**: muestreo periódico estricto por hardware (`TIM3 TRGO` $\rightarrow$ `ADC1` $\rightarrow$ `DMA1 Circular` + `DMAMUX1`) hacia un doble buffer Ping-Pong en `SRAM4` (`0x38000000`) sin jitter temporal y completamente desacoplado de la red.
2. El **Cortex-M7 (400 MHz, FPU DP, L1 I/D Cache)** aporta la **fuerza bruta de cálculo y conectividad**: ventana Blackman-Harris de 4 términos, **FFT de 4096 puntos** con CMSIS-DSP (`arm_rfft_fast_f32`), interpolación parabólica sub-bin, detección de hasta 10 armónicos (THD y SNR), **Transformada de Paquetes Wavelet (WPT)** Daubechies-4 de 4 niveles (16 sub-bandas uniformes de $800\text{ Hz}$, entropía de Shannon y detección de transitorios por factor de cresta) y la pila de red **lwIP RAW** emitiendo telemetría en tiempo real por Ethernet UDP hacia un **Dashboard Web interactivo**.

## Proyectos

| Carpeta | Qué es | Estado |
|---|---|---|
| [`01-h755-fft-udp/`](01-h755-fft-udp/) | Adquisición determinista a $f_s = 25.6\text{ kHz}$ ($N = 4096$) en el **M4** con `DMA1` circular Ping-Pong en `SRAM4`; análisis espectral FFT ($2048\text{ bins}$, $\Delta f = 6.25\text{ Hz}$), WPT de 16 sub-bandas uniformes y telemetría UDP en puerto `:5002` desde el **M7** | **Implementado y verificado** — Contrato IPC en `SRAM4` (`0x38000000`), ventana Blackman-Harris 4T + CMSIS-DSP RFFT, banco de filtros WPT `db4` con ordenamiento Gray/Paley, servidor UDP, script de flasheo dual-core 1-clic y **Dashboard Web en tiempo real** (osciloscopio virtual, analizador de espectro FFT, escalograma WPT en cascada a 60 Hz y filtros DSP) |

## Documentación Principal

- **[`01-h755-fft-udp/DOCS/REPORTE_TEORICO_DSP.md`](01-h755-fft-udp/DOCS/REPORTE_TEORICO_DSP.md)** — Fundamento teórico y matemático detallado de los algoritmos DSP implementados (FFT, ventana Blackman-Harris 4T, interpolación parabólica, banco de filtros QMF Daubechies-4, WPT de 16 bandas, código Gray y entropía de Shannon).
- **[`01-h755-fft-udp/Tools/web_dashboard/README.md`](01-h755-fft-udp/Tools/web_dashboard/README.md)** — Guía de uso y arquitectura modular del Dashboard Web en tiempo real (Node.js + SSE + Canvas 2D).
- **[`01-h755-fft-udp/Tools/COMO_FLASHEAR.md`](01-h755-fft-udp/Tools/COMO_FLASHEAR.md)** — Guía paso a paso para flashear ambos núcleos (`CM7` y `CM4`) mediante script de 1 clic o STM32CubeIDE.
- **[`Docs/ARQUITECTURA_DSP_ETH.md`](Docs/ARQUITECTURA_DSP_ETH.md)** — Guía transversal de arquitectura: reparto de núcleos, diseño de buffers Ping-Pong en `SRAM4` y configuración de la MPU.
- `../0_References/` — Hojas de datos y manuales de STMicroelectronics, compartidas con el resto de I+D.
- `../Signal Conditioning Piezoelectric Sensors/` — Frontend analógico de entrada: amplificadores de carga piezoeléctricos y aislamiento diferencial para inyectar en `PA3` (A0).

## Relación con el resto de I+D

```
I+D/
├── 0_References/                          Hojas de datos de ST, compartidas
├── 1_ADC/                                 Caracterización de ADC y calibración VREFINT
├── 2_Ethernet/                            Bring-up del MAC Ethernet y lwIP RAW
├── 3_DSP_Ethernet/                        Este repositorio: Dual-core ADC + CMSIS-DSP (FFT + WPT) + lwIP
└── Signal Conditioning Piezoelectric Sensors/  Frontend para sensores piezoeléctricos (vibración/carga)
```
