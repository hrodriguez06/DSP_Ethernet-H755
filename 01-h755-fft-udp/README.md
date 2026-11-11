# 01-h755-fft-udp: Adquisición Determinista en M4, FFT + Wavelet (WPT) y Telemetría UDP en M7

Sistema de adquisición y análisis espectral/tiempo-escala en tiempo real sobre la **NUCLEO-H755ZI-Q**, demostrando la **justificación arquitectónica del procesador dual-core STM32H755ZI**.

---

## 1. El Porqué de los Dos Núcleos

En instrumentación de señales procedentes de sensores piezoeléctricos (modo carga / acelerómetros / presión dinámica):
* **El Cortex-M4 (200 MHz, Bare-Metal):**
  - Muestreo periódico estricto por hardware (`TIM3 TRGO` $\rightarrow$ `ADC1` en canal `PA3` $\rightarrow$ `DMA1_Stream0` en modo circular con `DMAMUX1`).
  - **Cero jitter temporal**, fundamental para no degradar el piso de ruido de fase en la FFT.
  - Gestión automática por hardware de doble buffer Ping-Pong ($2 \times 4096$ muestras `uint16_t` = $16\text{ KB}$) en `SRAM4` (`0x38000000`) mediante interrupciones de media transferencia (`HTIF`) y transferencia completa (`TCIF`).
  - Duerme con `__WFI()` entre transferencias, minimizando el ruido digital conducido al plano analógico del ADC.
* **El Cortex-M7 (400 MHz, HAL + lwIP + CMSIS-DSP):**
  - Dueño del arranque y del árbol de relojes globales (400 MHz, `FLASH_LATENCY_4`).
  - Cachés L1 I/D activas (32 KB cada una) y FPU de doble precisión.
  - Región `SRAM4` configurada como **no cacheable** en la MPU para lectura determinista sin incoherencia de caché.
  - **Pipeline FFT:** Eliminación de offset DC, ventana **Blackman-Harris de 4 términos** ($-92\text{ dB}$ de lóbulo lateral), RFFT de **4096 puntos** con CMSIS-DSP (`arm_rfft_fast_f32`), interpolación parabólica sub-bin y detección de fundamental $f_0$, armónicos $2f_0 \dots 10f_0$, piso de ruido, SNR y THD.
  - **Pipeline Wavelet (WPT):** Transformada de Paquetes Wavelet **Daubechies-4 (`db4`)** de 4 niveles con árbol binario completo de **16 sub-bandas uniformes** ($\Delta f_{\text{WPT}} = 800\text{ Hz}$ por banda), reordenamiento espectral monótono mediante código Gray (orden de Paley), entropía de Shannon normalizada y detección de impactos/transitorios por factor de cresta.
  - **Pila de red lwIP RAW:** Emite telemetría UDP en el puerto `:5002` hacia el Dashboard Web y herramientas host.

---

## 2. Parámetros del Sistema DSP

| Parámetro | Valor |
|---|---|
| Pin de entrada analógica | `PA3` (`ADC1_INP15`, Conector Arduino `A0`) |
| Frecuencia de muestreo ($f_s$) | $25600\text{ Hz}$ ($25.6\text{ kHz}$) |
| Tamaño de bloque ($N$) | $4096\text{ muestras}$ |
| Período de bloque ($T_{\text{bloque}}$) | $160.0\text{ ms}$ ($6.25\text{ bloques/s}$) |
| Resolución espectral FFT ($\Delta f_{\text{FFT}}$) | $\frac{25600}{4096} = 6.25\text{ Hz}$ por bin |
| Rango de Nyquist | $0\text{ Hz}$ a $12.8\text{ kHz}$ ($2048\text{ bins espectrales}$) |
| Descomposición Wavelet (WPT) | $J = 4$ niveles (`db4`, 8 coeficientes QMF) $\rightarrow 16\text{ sub-bandas}$ |
| Ancho de cada sub-banda WPT ($\Delta f_{\text{WPT}}$) | $800.0\text{ Hz}$ ($256\text{ coeficientes por sub-banda}$) |

---

## 3. Estructura de la Memoria Compartida (SRAM4 `0x38000000`)

Declarada en [`Firmware/Fft_Eth_H755/Common/Inc/dsp_link.h`](Firmware/Fft_Eth_H755/Common/Inc/dsp_link.h):

```
  0x38000000  0x000: Contrato de arranque (magic, version, relojes, vrefint_cal, fs, block_size=4096)
  0x38000040  0x040: Zona de mando (cmd_seq, cmd_ack, cmd_run, cmd_fs_hz)
  0x38000080  0x080: Sincronización y telemetría (block_id, ready_idx, active_idx, flags)
  0x3800009C  0x09C: Métricas FFT publicadas por CM7 (f0, amplitud, ruido, SNR, THD, 10 armónicos, DC, Vpp)
  0x380000E0  0x0E0: Trampa de HardFault del CM4 (CFSR, HFSR, BFAR, PC, LR)
  0x380000F8  0x0F8: Métricas Wavelet WPT (energía % y RMS de 16 bandas, transitorios, factor cresta, entropía)
  0x38000200  0x200: Buffer Ping-Pong 0 (4096 x 2 bytes = 8192 B)
  0x38002200  0x2200: Buffer Ping-Pong 1 (4096 x 2 bytes = 8192 B)
```

---

## 4. Servidor UDP (Puerto 5002)

Comandos soportados vía datagrama UDP:
* `STREAM`: Inicia el flujo continuo de telemetría (datagramas binarios de 212 bytes con métricas FFT + WPT de 16 bandas).
* `STOP`: Detiene el flujo continuo de telemetría.
* `SPECTRUM`: Devuelve los 2048 bins de magnitud FFT en milivoltios fragmentados en 4 datagramas UDP.
* `WAVE`: Devuelve la ventana temporal de muestras crudas calibradas en milivoltios para el osciloscopio virtual.
* `STATUS`: Devuelve resumen en texto de las métricas FFT y WPT actuales.
* `FS <hz>`: Solicita al CM4 reconfigurar dinámicamente la frecuencia de muestreo.
* `HELP`: Muestra los comandos disponibles.

---

## 5. Herramientas, Flasheo y Visualización

### Dashboard Web en Tiempo Real (Recomendado)
Ubicado en [`Tools/web_dashboard/`](Tools/web_dashboard/README.md) (servidor ligero en Node.js sin dependencias externas + interfaz modular HTML5/Canvas/SSE):
```bash
cd Tools/web_dashboard
node server.js --ip 192.168.1.50 --port 5002
```
Abre `http://localhost:8088` en el navegador para visualizar:
1. **Osciloscopio Virtual** con disparo configurable y filtros digitales en tiempo real (Pasa-Bajos, Pasa-Altos, Pasa-Banda, Notch).
2. **Analizador de Espectro FFT de 2048 bins** ($\Delta f = 6.25\text{ Hz}$) y tabla de 10 armónicos.
3. **Analizador Multirresolución WPT de 16 sub-bandas** con **Escalograma 2D en cascada a 60 Hz**, entropía de Shannon y detector de transitorios.

### Flasheo Dual-Core en 1 Clic
Consulta [`Tools/COMO_FLASHEAR.md`](Tools/COMO_FLASHEAR.md) o ejecuta directamente:
```powershell
.\Tools\flash_dualcore.ps1
```

### Documentación Teórica
El desarrollo matemático completo de los algoritmos implementados se encuentra en [`DOCS/REPORTE_TEORICO_DSP.md`](DOCS/REPORTE_TEORICO_DSP.md).
