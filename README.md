# DSP & Ethernet — I+D

Proyectos de procesamiento digital de señales (DSP) y conectividad Ethernet sobre STM32H755ZI-Q. Cada proyecto es **autocontenido**: lleva su propio código, su proyecto de CubeIDE y sus herramientas, y compila sin depender de los demás. Lo único compartido es la documentación transversal y las hojas de datos.

Este repositorio materializa la **justificación arquitectónica del procesador dual-core**:
1. El **Cortex-M4** garantiza el **tiempo real duro**: muestreo periódico estricto por hardware (`TIMx TRGO` $\rightarrow$ `ADC1` $\rightarrow$ `DMA`) sin jitter temporal, completamente desacoplado de la red.
2. El **Cortex-M7** aporta la **fuerza bruta de cálculo y conectividad**: ventaneo, FFT de 1024 puntos con CMSIS-DSP (`arm_rfft_fast_f32`), detección de armónicos y la pila de red lwIP RAW emitiendo telemetría por Ethernet UDP.

## Proyectos

| Carpeta | Qué es | Estado |
|---|---|---|
| [`01-h755-fft-udp/`](01-h755-fft-udp/) | Adquisición periódica a $f_s = 25.6\text{ kHz}$ en el **M4** con DMA Ping-Pong en SRAM4; cálculo de FFT de 1024 puntos, detección de armónicos, THD y streaming UDP en puerto `:5002` desde el **M7** | **Diseñado e implementado** — Contrato Ping-Pong en SRAM4 (`0x38000000`), ventana Hanning + CMSIS-DSP RFFT, detector de fundamental y armónicos, servidor UDP y visualizador host en Python |

## Compartido

- **[`Docs/ARQUITECTURA_DSP_ETH.md`](Docs/ARQUITECTURA_DSP_ETH.md)** — La guía transversal del contenedor: reparto de núcleos, diseño de buffers Ping-Pong en SRAM4, tiempos de ejecución, configuración MPU y parámetros matemáticos.
- `../0_References/` — Hojas de datos y manuales de STMicroelectronics, compartidas con el resto de I+D.
- `../Signal Conditioning Piezoelectric Sensors/` — Frontend analógico de entrada: amplificadores de carga piezoeléctricos y aislamiento diferencial para inyectar en `PA3` (A0).

## Relación con el resto de I+D

```
I+D/
├── 0_References/                          Hojas de datos de ST, compartidas
├── 1_ADC/                                 Caracterización de ADC y calibración VREFINT
├── 2_Ethernet/                            Bring-up del MAC Ethernet y lwIP RAW
├── 3_DSP_Ethernet/                        Este repositorio: Dual-core ADC + CMSIS-DSP + lwIP
└── Signal Conditioning Piezoelectric Sensors/  Frontend para sensores piezoeléctricos (vibración/carga)
```
