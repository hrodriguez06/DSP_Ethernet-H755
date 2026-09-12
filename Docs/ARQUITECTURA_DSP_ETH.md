# Arquitectura DSP + Ethernet sobre STM32H755 Dual-Core

Este documento establece la arquitectura transversal para el procesamiento de señales en tiempo real y transmisión de telemetría espectral por Ethernet en la NUCLEO-H755ZI-Q.

---

## 1. La Justificación del Doble Núcleo

En instrumentación analógica de alta fidelidad y mantenimiento predictivo (vibración de maquinaria con acelerómetros piezoeléctricos), se exige simultáneamente:
1. **Adquisición temporal con cero jitter:** La transformada de Fourier discreta asume que el intervalo de muestreo $\Delta t = 1/f_s$ es matemáticamente exacto. El jitter en el trigger del ADC modula en fase la señal y eleva el piso de ruido espectral (*phase noise floor*).
2. **Cálculo matemático intensivo:** Ventaneo, RFFT en punto flotante, cálculo de magnitudes y búsqueda de picos armónicos.
3. **Pila de red IP asíncrona:** Recepción y transmisión de tramas Ethernet con lwIP, con tráfico de red imprevisto (ARP, broadcasts, ráfagas).

En un microcontrolador de un solo núcleo, estas tres funciones compiten por la CPU. La atención de un paquete de red o una ejecución de FFT puede demorar la recolección del ADC o forzar el uso de buffers desproporcionados, introduciendo jitter de software y latencias erráticas.

En el **STM32H755**, el reparto físico resuelve el conflicto:

```
  SEÑAL ANALÓGICA (PA3)
          │
          ▼
  ┌────────────────────────────────────────────────────────┐
  │                 CORTEX-M4 (200 MHz)                    │
  │  • Motor de Tiempo Real Duro                           │
  │  • TIM3 TRGO @ fs = 25.6 kHz (jitter < 5 ns)           │
  │  • ADC1 16-bit Regular Channel                         │
  │  • DMA Ping-Pong a SRAM4 (0x38000000)                  │
  │  • Cero interrupciones de red ni cálculos pesados      │
  │  • Carga de CPU: < 1 %                                 │
  └──────────────────────────┬─────────────────────────────┘
                             │
                  MEMORIA COMPARTIDA SRAM4
                  Doble Buffer (2 x 1024 uint16_t = 4 KB)
                  Flags de sincronización (seqlock/block_id)
                  (MPU: Non-Cacheable en CM7)
                             │
                             ▼
  ┌────────────────────────────────────────────────────────┐
  │                 CORTEX-M7 (400 MHz)                    │
  │  • Motor DSP y de Conectividad                         │
  │  • Conversión LSB -> mV usando VDDA real de VREFINT    │
  │  • Ventana Hanning (mitiga spectral leakage)           │
  │  • CMSIS-DSP: arm_rfft_fast_f32 (1024 puntos)          │
  │  • arm_cmplx_mag_f32 (512 bins espectrales)            │
  │  • Detección de f0, armónicos 2f0..5f0 y THD           │
  │  • Pila lwIP RAW (MAC RMII + PHY LAN8742A)             │
  │  • Streaming UDP continuo (:5002)                      │
  │  • Tiempo de cómputo DSP: ~60 µs / bloque (0.15 % CPU) │
  └──────────────────────────┬─────────────────────────────┘
                             │
                             ▼
                   ETHERNET UDP STREAM
```

---

## 2. Parámetros Numéricos del Análisis Espectral

Para instrumentación de sensores piezoeléctricos (vibración de maquinaria según ISO 10816, detección de fallas en rodamientos y engranajes):

| Parámetro | Valor | Justificación Técnica |
|---|---|---|
| **Frecuencia de muestreo ($f_s$)** | $25600\text{ Hz}$ ($25.6\text{ kHz}$) | Estándar en vibraciones industriales. Permite registrar hasta armónicos ultrasónicos tempranos. |
| **Tamaño de bloque ($N$)** | $1024\text{ muestras}$ | Potencia de 2 óptima para RFFT radix-4/radix-8 en CMSIS-DSP. |
| **Resolución en frecuencia ($\Delta f$)** | $\frac{f_s}{N} = \frac{25600}{1024} = 25.0\text{ Hz}$ | Cada bin del espectro representa exactamente 25 Hz. |
| **Ancho de banda útil (Nyquist)** | $\frac{f_s}{2} = 12.8\text{ kHz}$ | 512 bins espectrales útiles ($0\text{ Hz}$ hasta $12775\text{ Hz}$). |
| **Periodo de bloque ($T_{\text{bloque}}$)** | $\frac{N}{f_s} = \frac{1024}{25600} = 40.0\text{ ms}$ | El sistema genera 25 espectros completos por segundo. |

### Presupuesto de Tiempo de CPU en el Cortex-M7

A $400\text{ MHz}$ con cachés I-Cache y D-Cache habilitadas:
* Conversión entera $\rightarrow$ `float32_t` + Ventana Hanning ($1024$ puntos): $\approx 12\ \mu\text{s}$.
* `arm_rfft_fast_f32` ($1024$ puntos reales): $\approx 28\ \mu\text{s}$.
* `arm_cmplx_mag_f32` ($512$ bins): $\approx 14\ \mu\text{s}$.
* Algoritmo de detección de picos y armónicos: $\approx 6\ \mu\text{s}$.
* **Tiempo total DSP por bloque:** $\approx 60\ \mu\text{s}$.

$$\text{Ocupación de CPU DSP} = \frac{60\ \mu\text{s}}{40000\ \mu\text{s}} = 0.15\%$$

El Cortex-M7 dispone del **$99.85\%$ de su tiempo** para gestionar el tráfico Ethernet lwIP, atender solicitudes UDP y la consola de depuración sin ninguna contención.

---

## 3. La Frontera en SRAM4 (`0x38000000`)

La SRAM4 (dominio D3, 64 KB) es el medio de intercambio ideal:
1. **Misma dirección absoluta:** Ambos núcleos la direccionan en `0x38000000`.
2. **Independencia de reloj:** No depende de los bits de reloj de dominio D1 o D2; está siempre activa.
3. **Buffer Ping-Pong:**
   - Buffer 0: `0x38000200` ($1024 \times 2 = 2048\text{ bytes}$).
   - Buffer 1: `0x38000A00` ($1024 \times 2 = 2048\text{ bytes}$).
   - Total para muestras: $4096\text{ bytes}$ (apenas un 6.25% de la SRAM4).
4. **Regla de Caché MPU (Crucial):**
   - El Cortex-M7 tiene D-Cache activa. Si la SRAM4 fuera cacheable, el M7 leería siempre las muestras viejas en su caché L1.
   - En `mpu_config.c` se programa la región MPU cubriendo `0x38000000` con atributo `MPU_ACCESS_NOT_CACHEABLE`.
   - En el Cortex-M4 no hay D-Cache, pero se ejecuta `__DSB()` tras escribir el buffer y conmutar el flag `block_id`.

---

## 4. Algoritmo de Detección de Señales Periódicas

1. **Ventaneo Hanning:**
   $$w[n] = 0.5 - 0.5 \cos\left(\frac{2\pi n}{N}\right), \quad 0 \le n < N$$
   Atenúa a cero los extremos del bloque temporal para evitar saltos artificiales y fugas espectrales (*spectral leakage*).
2. **Transformada RFFT:**
   CMSIS-DSP `arm_rfft_fast_f32` calcula la RFFT de tamaño $N=1024$ generando $N/2$ números complejos ordenados: $\text{Re}[0], \text{Im}[0], \dots, \text{Re}[511], \text{Im}[511]$.
3. **Magnitud Espectral:**
   $$|X[k]| = \frac{2}{N} \sqrt{\text{Re}[k]^2 + \text{Im}[k]^2}$$
   El factor $\frac{2}{N}$ normaliza la amplitud espectral al valor pico en el dominio del tiempo.
4. **Estimación del Piso de Ruido ($V_{\text{noise}}$):**
   Se calcula la media de los bins descartando el componente DC ($k=0$) y los 5 bins de mayor amplitud.
5. **Detección de la Fundamental ($f_0$):**
   Se busca el pico de mayor amplitud $|X[k_{\text{peak}}]|$ entre $k=1$ y $511$.
   Si $|X[k_{\text{peak}}]|$ supera $V_{\text{noise}} \times 3.0$ (umbral de $\approx 10\text{ dB}$ sobre el ruido), se declara presencia de componente periódica con frecuencia:
   $$f_0 = k_{\text{peak}} \times \Delta f = k_{\text{peak}} \times 25.0\text{ Hz}$$
6. **Seguimiento de Armónicos ($2f_0, 3f_0, 4f_0, 5f_0$):**
   Para cada armónico entero $m \in \{2, 3, 4, 5\}$, se busca la energía máxima en una ventana de tolerancia alrededor de $m \cdot k_{\text{peak}}$.
7. **Cálculo de Distorsión Armónica Total (THD):**
   $$\text{THD} = \frac{\sqrt{\sum_{m=2}^{5} V_{m}^2}}{V_1} \times 100\%$$

---

## 5. Protocolo de Telemetría UDP

El Cortex-M7 abre el socket UDP en el puerto **`5002`**.
Emite periódicamente datagramas binarios compactos hacia la IP destino:

### Estructura del Paquete de Telemetría (`DSP_Packet_t` - 64 bytes)

```c
typedef struct __attribute__((packed))
{
  uint32_t magic;           /* 0x44535032 ("DSP2") */
  uint32_t block_id;        /* Contador monotónico de bloques */
  uint32_t timestamp_ms;    /* SysTick del Cortex-M4 al completar muestreo */
  uint32_t fs_hz;           /* 25600 */
  uint32_t n_fft;           /* 1024 */
  uint32_t f0_hz_x10;       /* Frecuencia fundamental en décimas de Hz (ej: 10000 = 1000.0 Hz) */
  uint32_t f0_mag_mv;       /* Amplitud pico de f0 en milivoltios */
  uint32_t noise_floor_mv;  /* Piso de ruido estimado en mV */
  uint32_t snr_db_x10;      /* SNR en décimas de dB */
  uint32_t thd_pct_x10;     /* THD en décimas de % (ej: 35 = 3.5 %) */
  uint32_t flags;           /* Bit 0: Señal periódica detectada, Bit 1: Clipping/Overflow */
  uint16_t harm_mag_mv[5];  /* Amplitudes de armónicos 1 a 5 */
  uint16_t rsv[3];          /* Relleno para alineación a 64 bytes */
} DSP_TelemetryPacket_t;
```

Opcionalmente, enviando el comando UDP `SPECTRUM` al microcontrolador, el M7 envía el vector completo de 512 bins de magnitud para alimentar el visualizador gráfico en PC.
