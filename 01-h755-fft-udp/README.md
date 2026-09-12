# 01-h755-fft-udp: Adquisición Periódica en M4, FFT y Telemetría UDP en M7

Ejercicio de adquisición y análisis espectral en tiempo real sobre la **NUCLEO-H755ZI-Q**, demostrando la **justificación arquitectónica del procesador dual-core**.

---

## 1. El Porqué de los Dos Núcleos

En instrumentación de señales procedentes de sensores piezoeléctricos (modo carga / acelerómetros / presión dinámica):
* **El Cortex-M4 (200 MHz, Bare-Metal):**
  - Muestreo periódico estricto por hardware (`TIM3 TRGO` $\rightarrow$ `ADC1` regular channel en `PA3`).
  - **Cero jitter temporal**, fundamental para no degradar el piso de ruido de fase en la FFT.
  - Gestión directa de buffer Ping-Pong ($2 \times 1024$ muestras `uint16_t` = 4 KB) en `SRAM4` (`0x38000000`).
  - Duerme con `__WFI()` entre muestras, minimizando el ruido conducido al plano de masa del ADC.
* **El Cortex-M7 (400 MHz, HAL + lwIP + CMSIS-DSP):**
  - Dueño del arranque y del árbol de relojes globales (400 MHz, `FLASH_LATENCY_4`).
  - Caches L1 I/D activas (32 KB cada una).
  - Región `SRAM4` programada como **no cacheable** en la MPU para lectura sin contención de datos.
  - Ventana Hanning y ejecución de RFFT de 1024 puntos con CMSIS-DSP (`arm_rfft_fast_f32`) en $\approx 60\ \mu\text{s}$ (apenas $0.15\%$ de su CPU a $f_s = 25.6\text{ kHz}$).
  - Algoritmo de detección armónica: fundamental $f_0$, armónicos $2f_0 \dots 5f_0$, estimación de piso de ruido y THD.
  - Pila de red lwIP RAW: emite datagramas UDP en el puerto `:5002` a 25 Hz hacia herramientas host.

---

## 2. Parámetros del Análisis Espectral

| Parámetro | Valor |
|---|---|
| Pin de entrada | `PA3` (Conector A0) |
| Frecuencia de muestreo ($f_s$) | $25600\text{ Hz}$ ($25.6\text{ kHz}$) |
| Tamaño de bloque ($N$) | $1024\text{ muestras}$ |
| Resolución en frecuencia ($\Delta f$) | $\frac{25600}{1024} = 25.0\text{ Hz}$ por bin |
| Rango de Nyquist | $0\text{ Hz}$ a $12.8\text{ kHz}$ (512 bins espectrales) |
| Cadencia de bloque | $40.0\text{ ms}$ ($25\text{ bloques por segundo}$) |

---

## 3. Estructura de la Memoria Compartida (SRAM4 `0x38000000`)

Declarada en [`Common/Inc/dsp_link.h`](Common/Inc/dsp_link.h):

```
  0x38000000  0x000: Contrato de arranque (magic, version, relojes, vrefint_cal, fs)
  0x38000040  0x040: Zona de mando (cmd_seq, cmd_ack, cmd_run, cmd_fs_hz)
  0x38000080  0x080: Sincronizacion y telemetria (block_id, ready_idx, active_idx, flags)
  0x3800009C  0x09C: Metricas DSP publicadas por CM7 (f0, amplitud, ruido, SNR, THD)
  0x380000C4  0x0C4: Trampa de HardFault del CM4 (CFSR, HFSR, BFAR, PC, LR)
  0x38000200  0x200: Buffer Ping-Pong 0 (1024 x 2 bytes = 2048 B)
  0x38000A00  0xA00: Buffer Ping-Pong 1 (1024 x 2 bytes = 2048 B)
```

---

## 4. Servidor UDP (Puerto 5002)

Comandos soportados vía datagrama UDP:
* `STREAM`: Inicia el flujo continuo de telemetría espectral (datagramas binarios de 64 bytes a 25 Hz).
* `STOP`: Detiene el flujo de telemetría.
* `SPECTRUM`: Devuelve el paquete completo de 512 bins de magnitud en milivoltios.
* `STATUS`: Devuelve texto descriptivo del estado actual de detección.
* `FS <hz>`: Solicita al CM4 reconfigurar la frecuencia de muestreo.
* `HELP`: Muestra los comandos disponibles.

---

## 5. Visualizador Host en Python

En [`Tools/dsp_visualizer.py`](Tools/dsp_visualizer.py):
```bash
python Tools/dsp_visualizer.py --ip 192.168.1.50 --port 5002
```
Muestra en consola la tabla de telemetría en tiempo real:
```
  Bloque |   Estado   |   f0 (Hz) | Ampl (mV) | Ruido (mV) |  THD (%) |  SNR (dB)
--------------------------------------------------------------------------------
     120 | PERIODICA  |    1000.0 |       850 |          4 |      1.2 |     46.5
     121 | PERIODICA  |    1000.0 |       850 |          4 |      1.2 |     46.5
```
