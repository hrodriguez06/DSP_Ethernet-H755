# Reporte Técnico: Fundamento Teórico y Matemático de los Algoritmos DSP en `01-h755-fft-udp`

**Proyecto:** `01-h755-fft-udp`  
**Plataforma:** NUCLEO-H755ZI-Q (Arquitectura Dual-Core STM32H755ZI)  
**Núcleos:** ARM Cortex-M4 (200 MHz, muestreo determinista) & ARM Cortex-M7 (400 MHz, motor DSP + lwIP)  
**Dominio de Aplicación:** Análisis de vibraciones mecánicas y señales piezoeléctricas (sensores dinámicos de presión y aceleración)  

---

## 1. Introducción y Contexto Arquitectónico

El sistema implementa una arquitectura desacoplada por hardware:
* **Cortex-M4 (Bare-Metal a 200 MHz):** Adquisición periódica estricta sin jitter mediante `TIM3 TRGO` sincronizado con el `ADC1` (muestras en canal `PA3`). Gestiona un buffer Ping-Pong en la memoria compartida `SRAM4` (`0x38000000`) y duerme en `__WFI()` entre muestras para minimizar el ruido conducido al conversor.
* **Cortex-M7 (400 MHz, FPU DP, Cachés L1 I/D activas):** Ejecuta de forma asíncrona y continua dos pipelines DSP sobre cada bloque lleno: **Transformada Rápida de Fourier (FFT)** y **Transformada de Paquetes Wavelet (WPT)**, transmitiendo los resultados por UDP a través de la pila lwIP.

### Parámetros Globales del Sistema

| Parámetro | Símbolo | Valor | Justificación Técnica |
|---|---|---|---|
| Frecuencia de Muestreo | $f_s$ | $25600\text{ Hz}$ ($25.6\text{ kHz}$) | Captura de eventos acústicos y vibratorios hasta $12.8\text{ kHz}$ |
| Tamaño de Bloque | $N$ | $4096\text{ muestras}$ | Compromiso óptimo entre resolución espectral y latencia temporal |
| Período de Bloque | $T_{\text{bloque}}$ | $160.0\text{ ms}$ | $N / f_s = 4096 / 25600\text{ s}$ |
| Resolución Espectral FFT | $\Delta f_{\text{FFT}}$ | $6.25\text{ Hz}$ | $f_s / N = 25600 / 4096\text{ Hz}$ |
| Límite de Nyquist | $f_{\text{Nyq}}$ | $12800\text{ Hz}$ ($12.8\text{ kHz}$) | $f_s / 2$ |
| Niveles Wavelet | $J$ | $4$ niveles | $2^4 = 16$ sub-bandas uniformes en WPT |
| Ancho de Sub-banda WPT | $\Delta f_{\text{WPT}}$ | $800.0\text{ Hz}$ | $f_{\text{Nyq}} / 16 = 12800 / 16\text{ Hz}$ |
| Coeficientes por Sub-banda | $L_{\text{pkt}}$ | $256\text{ coeficientes}$ | $4096 / 16$ |

---

## 2. Términos y Conceptos Fundamentales

Esta sección compila de forma concisa los conceptos teóricos indispensables para comprender el procesamiento implementado:

### 2.1 Dominio Frecuencial y Análisis Espectral (FFT)
* **DFT / FFT (Transformada Rápida de Fourier):** Algoritmo que descompone una señal discreta finita en el tiempo en una combinación lineal de sinusoides ortogonales complejas ($e^{-j\frac{2\pi}{N}kn}$), revelando la distribución de energía en el dominio de la frecuencia.
* **Simetría Hermítica:** Propiedad matemática intrínseca a toda transformada de Fourier sobre señales puramente reales ($x[n] \in \mathbb{R}$), donde los coeficientes espectrales de frecuencias negativas son el conjugado de las positivas ($X[N-k] = X^*[k]$). Permite prescindir de la mitad superior del espectro y calcular la FFT consumiendo la mitad de memoria y operaciones.
* **Fuga Espectral (*Spectral Leakage*):** Deformación y ensanchamiento espectral que ocurre cuando una componente senoidal no tiene un número entero exacto de ciclos dentro de la ventana de análisis. Al truncar la señal, su energía se "derrama" hacia los bines contiguos, enmascarando armónicos débiles.
* **Enventanado (*Windowing*):** Ponderación temporal que multiplica la señal por una función suave que decae a cero en los extremos ($x_w[n] = x[n] \cdot w[n]$). Reduce la discontinuidad de borde artificial creada por el truncamiento temporal y minimiza la fuga espectral.
* **Ganancia Coherente (*Coherent Gain*):** Factor de atenuación media introducido por la ventana sobre la componente fundamental. Debe compensarse matemáticamente para que las magnitudes espectrales obtenidas coincidan rigurosamente con los milivoltios pico reales.
* **Resolución Espectral ($\Delta f$):** Distancia mínima en Hertz entre dos bines consecutivos de la FFT ($\Delta f = f_s / N$). Representa la discretización del eje de frecuencias.
* **Interpolación Sub-Bin:** Método analítico (generalmente parabólico) que interpola el lóbulo principal entre los bines discretos adyacentes para hallar la frecuencia continua real de un pico armónico, superando el límite de cuantización de la rejilla sin requerir mayor longitud $N$.
* **Piso de Ruido (*Noise Floor*):** Nivel medio de energía espectral estocástica de fondo generada por ruido térmico, cuantización del ADC y fluctuaciones ambientales, excluyendo los componentes armónicos deterministas.
* **THD (Distorsión Armónica Total):** Parámetro normalizado que cuantifica la pureza de una señal periódica, midiendo la relación cuadrática entre la suma de la potencia de los armónicos superiores ($2f_0, 3f_0, \dots$) y la potencia de la fundamental ($f_0$).
* **SNR (Relación Señal/Ruido):** Relación logarítmica (en decibeles) entre la amplitud del pico armónico dominante y el nivel medio del piso de ruido.

### 2.2 Dominio Tiempo-Escala y Multirresolución (Wavelet)
* **Principio de Incertidumbre de Heisenberg-Gabor:** Límite físico según el cual no es posible conocer simultáneamente con precisión arbitraria el instante temporal y la frecuencia de un evento ($\Delta t \cdot \Delta f \ge \frac{1}{4\pi}$).
* **Wavelet (Ondícula):** Función oscilatoria transitoria $\psi(t)$ de energía finita, de duración acotada en el tiempo (soporte compacto) y con media cero ($\int \psi(t)dt = 0$). Actúa como una ventana que se contrae para altas frecuencias y se expande para bajas frecuencias.
* **Multirresolución:** Técnica que descompone una señal en aproximaciones gruesas y detalles progresivamente más finos mediante escalas diádicas sucesivas.
* **DWT vs. WPT:** 
  * En la **DWT convencional** (esquema piramidal de Mallat), solo se descompone la rama pasa-bajos en cada nivel, produciendo bandas de frecuencia espaciadas logarítmicamente (alta resolución temporal en altas frecuencias, pero pobre resolución espectral).
  * En la **WPT (Transformada de Paquetes Wavelet)**, se descomponen **tanto las ramas pasa-bajos como las pasa-altos**, generando un árbol binario completo donde todas las sub-bandas terminales tienen el mismo ancho de banda uniforme ($\Delta f_{\text{banda}} = \text{constante}$).
* **Banco de Filtros QMF (Cuadratura de Espejo):** Par de filtros digitales ortonormales conjugados (pasa-bajos $H$ y pasa-altos $G$) que permiten dividir y reconstruir una señal sin pérdida de información ni solapamiento (*aliasing*).
* **Momentos de Desvanecimiento (*Vanishing Moments*):** Propiedad de una base wavelet que anula automáticamente polinomios temporales de orden $p-1$. Permite que tendencias lentas, derivas y rampas desaparezcan de los coeficientes de detalle, aislando fluctuaciones puramente anómalas.
* **Inversión Espectral:** Efecto físico derivado del submuestreo de factor 2 tras un filtrado pasa-altos: las frecuencias más altas de la banda original se despliegan sobre las frecuencias bajas del nuevo sub-nodo, invirtiendo el orden de las frecuencias resultantes.
* **Código Gray (Mapeo de Paley):** Algoritmo de reordenamiento de los nodos de salida del árbol WPT que revierte la inversión espectral y entrega las sub-bandas ordenadas de manera estrictamente monótona en frecuencia.
* **Entropía de Shannon Wavelet:** Medida probabilística del grado de dispersión o concentración energética entre las sub-bandas de la WPT. Cuantifica si la señal corresponde a una oscilación monocromática estable o a una condición de degradación/ruido aleatorio.
* **Factor de Cresta (*Crest Factor*):** Cociente adimensional entre el valor pico absoluto de una señal y su valor eficaz RMS ($\text{CF} = |x|_{\max} / \text{RMS}$). Es el indicador por excelencia para detectar pulsos e impactos transitorios de corta duración en vibraciones mecánicas.

---

## 3. Fundamento Teórico y Matemático del Motor FFT (`dsp_engine.c`)

```
   Señal Cruda ADC (4096 pts)
               │
               ▼
   Conversión ADC -> mV y Remoción de DC (media)
               │
               ▼
   Ventana Blackman-Harris 4-Términos (-92 dB)
               │
               ▼
   Real FFT CMSIS-DSP (arm_rfft_fast_f32)
               │
               ▼
   Magnitud Compleja y Escalamiento Coherente (mV pk)
               │
       ┌───────┴────────────────────────┐
       ▼                                ▼
   Interpolación Parabólica        Seguimiento Armónico
   Sub-Bin (f0 continuo)           (2f0..10f0, THD, SNR)
```

### 3.1 Transformada Discreta de Fourier y Algoritmo Real FFT

La Transformada Discreta de Fourier (DFT) mapea la secuencia discreta $x[n]$ al dominio frecuencial $X[k]$:

$$X[k] = \sum_{n=0}^{N-1} x[n] e^{-j \frac{2\pi}{N} kn}, \quad k = 0, 1, \dots, N-1$$

Donde cada bin $k$ corresponde a la frecuencia analógica $f_k = k \cdot \Delta f = k \frac{f_s}{N}$.

#### Simetría Hermítica y CMSIS-DSP Real FFT
Dado que las muestras del ADC son puramente reales ($x[n] \in \mathbb{R}$), los coeficientes espectrales satisfacen:

$$X[N - k] = X^*[k]$$

Por lo tanto, la mitad superior del espectro ($k > N/2$) contiene información duplicada y conjugada. El algoritmo CMSIS-DSP `arm_rfft_fast_f32`:
1. Reorganiza los $N$ puntos reales como una secuencia compleja de $N/2$ puntos:
   $$z[m] = x[2m] + j \cdot x[2m+1], \quad m = 0, \dots, N/2 - 1$$
2. Ejecuta una FFT compleja Radix-8 / Radix-4 Split-Radix sobre $z[m]$.
3. Aplica una etapa de desacoplamiento algebraico (*twiddle stage*) en tiempo lineal $\mathcal{O}(N)$ para extraer los coeficientes de Fourier de la señal original.

Esto reduce a la mitad el número de multiplicaciones complejas y el consumo de memoria respecto a una FFT estándar.

### 3.2 Fuga Espectral y Ventana Blackman-Harris de 4 Términos

Al muestrear un bloque finito de longitud $N$, la señal se multiplica implícitamente por una ventana rectangular $w_R[n]$. En el dominio de la frecuencia, esto equivale a convolucionar el espectro teórico continuo $X(\omega)$ con la respuesta espectral de la ventana:

$$W_R(\omega) = e^{-j\omega(N-1)/2} \frac{\sin(\omega N / 2)}{\sin(\omega / 2)}$$

Los lóbulos secundarios de la ventana rectangular solo se atenúan a $-13.3\text{ dB}$, con una tasa de decaimiento lenta de $-6\text{ dB/octava}$. Si una componente senoidal no coincide con un múltiplo entero de $\Delta f$, su energía se derrama ampliamente en el espectro.

#### Formulación de la Ventana Blackman-Harris 4T
Para garantizar un piso de ruido limpio y detectar armónicos de muy baja amplitud, se implementó la **ventana Blackman-Harris de 4 términos**:

$$w[n] = a_0 - a_1 \cos\left(\frac{2\pi n}{N-1}\right) + a_2 \cos\left(\frac{4\pi n}{N-1}\right) - a_3 \cos\left(\frac{6\pi n}{N-1}\right), \quad n \in [0, N-1]$$

Coeficientes exactos calculados en `DSP_Engine_Init()`:
$$a_0 = 0.35875, \quad a_1 = 0.48829, \quad a_2 = 0.14128, \quad a_3 = 0.01168$$

Ventajas determinantes:
* **Atenuación del lóbulo secundario:** $-92.0\text{ dB}$ (en contraste con $-31.5\text{ dB}$ en Hanning o $-42.7\text{ dB}$ en Hamming).
* **Tasa de caída:** $-18\text{ dB/octava}$, lo que aísla de inmediato componentes espurias adyacentes.

#### Compensación de Ganancia Coherente (*Coherent Gain*)
Al atenuar la señal en los extremos, la ventana reduce la amplitud aparente. La **ganancia coherente** ($CG$) es el valor medio de la ventana:

$$CG = \frac{1}{N} \sum_{n=0}^{N-1} w[n] = a_0 = 0.35875$$

Para restaurar la amplitud pico en milivoltios de una componente sinusoidal pura, la magnitud compleja $|X[k]| = \sqrt{\text{Re}^2 + \text{Im}^2}$ se multiplica por el factor de escala:

$$\text{Factor}_{\text{norm}} = \frac{2}{CG \cdot N} = \frac{2}{0.35875 \cdot N}$$

El factor $2$ proviene de sumar la energía de la banda de frecuencia negativa conjugada. Para el término puramente continuo (bin DC $k=0$), no existe término simétrico y el factor es simplemente $1/N$.

### 3.3 Interpolación Parabólica Sub-bin (Corrección de Frecuencia Fina)

Con $\Delta f = 6.25\text{ Hz}$, una señal de $1000.0\text{ Hz}$ coincide con el bin 160. Sin embargo, una vibración de $1002.3\text{ Hz}$ caería entre los bines 160 y 161, introduciendo un error de cuantización espectral (*scalloping loss*).

Para obtener la frecuencia exacta sin el costo de aplicar *Zero-Padding*, el lóbulo principal de la ventana se modela mediante una parábola continua $P(x) = A(x - \delta)^2 + C$ en escala de magnitudes lineales:

Sean:
* $y_1 = |X[k_{\text{peak}} - 1]|$ (magnitud en el bin anterior)
* $y_2 = |X[k_{\text{peak}}]|$ (magnitud en el bin pico dominante)
* $y_3 = |X[k_{\text{peak}} + 1]|$ (magnitud en el bin posterior)

El desplazamiento fraccionario continuo del vértice $\delta \in [-0.5, +0.5]$ respecto al centro del bin $k_{\text{peak}}$ se deduce analíticamente:

$$\delta = \frac{y_3 - y_1}{2(2y_2 - y_1 - y_3)}$$

La frecuencia fundamental corregida $f_0$ resulta:

$$f_0 = (k_{\text{peak}} + \delta) \cdot \Delta f = (k_{\text{peak}} + \delta) \cdot \frac{f_s}{N}$$

Este procedimiento reduce el error de estimación de frecuencia a típicamente $< 0.1\text{ Hz}$ en señales con $\text{SNR} > 20\text{ dB}$.

### 3.4 Estimación de Métricas Espectrales: THD, SNR y Piso de Ruido

1. **Piso de Ruido Robusto:**
   Se calcula excluyendo la componente continua/térmica ($k < 2$, correspondiente a frecuencias $< 12.5\text{ Hz}$) y el bin dominante:
   $$\text{NoiseFloor} = \frac{\left(\sum_{k=2}^{N/2-1} |X[k]|\right) - |X[k_{\text{peak}}]|}{(N/2 - 3)}$$

2. **Seguimiento Armónico con Tolerancia Local:**
   Los armónicos teóricos ocurren en $h \cdot f_0$ para $h \in [2, 10]$. Por efectos de dispersión, se busca el valor máximo en un entorno de $\pm 1$ bin respecto al centro teórico $k_h = \text{round}(h \cdot k_{\text{peak}})$:
   $$V_h = \max \left( |X[k_h - 1]|, |X[k_h]|, |X[k_h + 1]| \right)$$

3. **Distorsión Armónica Total (THD):**
   $$\text{THD} = \frac{\sqrt{\sum_{h=2}^{10} V_h^2}}{V_1} \times 100\%$$

4. **Relación Señal a Ruido (SNR):**
   $$\text{SNR}_{\text{dB}} = 20 \log_{10}\left(\frac{V_1}{\text{NoiseFloor}}\right)$$

---

## 4. Fundamento Teórico y Matemático del Motor Wavelet WPT (`dsp_wavelet.c`)

```
                        Señal x[n] (N = 4096)
                                  │
                   Nivel 1 ───────┴───────
                          │               │
                        H │               │ G (Detalle HF D1: 6.4-12.8 kHz)
                          ▼               ▼
                        (2048)          (2048) ---> Detección de Transitorios
                          │               │         (Crest Factor, Offset us)
                   Nivel 2 ───┴───         ───┴───
                          │       │       │       │
                        (1024)  (1024)  (1024)  (1024)
                          │       │       │       │
                   Nivel 3 ───     ───     ───     ─── (8 nodos x 512 muestras)
                          │       │       │       │
                   Nivel 4 ───     ───     ───     ─── (16 sub-bandas x 256 muestras)
                                  │
                                  ▼
                   Permutación Código Gray (Paley)
                     [B0, B1, B2, ..., B15] ordenadas en freq (800 Hz/banda)
                                  │
                                  ▼
                   Métricas: Energía, RMS, Densidad y Entropía de Shannon
```

### 4.1 Limitaciones de la FFT y Necesidad de las Wavelets

La Transformada de Fourier proyecta la señal sobre sinusoides de duración infinita ($e^{j\omega t}$). Como consecuencia del principio de Heisenberg-Gabor:

$$\Delta t \cdot \Delta f \ge \frac{1}{4\pi}$$

La FFT posee excelente selectividad en frecuencia $\Delta f$, pero carece por completo de localización temporal ($\Delta t = \infty$): un pulso impulsivo o falla transitoria de $20\ \mu\text{s}$ altera la fase y eleva el piso de todos los bines del bloque, impidiendo determinar **cuándo** ocurrió la anomalía.

### 4.2 DWT Clásica vs. Transformada de Paquetes Wavelet (WPT)

* **DWT Estándar (Esquema Piramidal de Mallat):** Descompone iterativamente solo la aproximación (pasa-bajos), generando una partición diádica (logarítmica). Esto impide realizar un análisis de banda estrecha en frecuencias medias y altas.
* **Transformada de Paquetes Wavelet (WPT):** Descompone de forma estrictamente simétrica **tanto las aproximaciones como los detalles** en cada nivel.
  * Para $J = 4$ niveles, genera $2^4 = 16$ sub-bandas ortogonales de igual ancho de banda:
    $$\Delta f_{\text{WPT}} = \frac{f_{\text{Nyquist}}}{2^J} = \frac{12800\text{ Hz}}{16} = 800.0\text{ Hz}$$
  * Cada paquete contiene $L = N / 2^J = 4096 / 16 = 256$ coeficientes.

### 4.3 Banco de Filtros Ortonormales Daubechies-4 (`db4`)

La familia Daubechies es la base ortonormal con soporte compacto que ofrece el mayor número de momentos de desvanecimiento para una longitud de filtro determinada. Para `db4`, la longitud es de 8 taps con $p = 4$ momentos de desvanecimiento:

$$\int_{-\infty}^{\infty} t^k \psi(t) dt = 0, \quad k = 0, 1, 2, 3$$

Esto asegura que cualquier componente polinómica de orden menor a 4 (como tendencias mecánicas de baja frecuencia o derivas térmicas) es completamente bloqueada por los filtros pasa-altos, dejando en los coeficientes de detalle únicamente transitorios y vibraciones puras.

#### Coeficientes y Banco de Filtros QMF
Los filtros de análisis satisfacen las condiciones de cuadratura de espejo conjugada:
$$\sum_{k=0}^7 h[k] = \sqrt{2}, \quad \sum_{k=0}^7 h^2[k] = 1.0, \quad \sum_{k=0}^7 g[k] = 0, \quad \langle h, g \rangle = 0$$

Para cada nodo $j$ en el nivel $l$, los coeficientes de los hijos izquierdo (pasa-bajos) y derecho (pasa-altos) se obtienen mediante convolución circular periódica y decimación por factor 2 ($\downarrow 2$):

$$c_{l+1, 2j}[n] = \sum_{k=0}^7 h[k] \cdot c_{l, j}[(2n + k) \pmod M]$$

$$d_{l+1, 2j+1}[n] = \sum_{k=0}^7 g[k] \cdot c_{l, j}[(2n + k) \pmod M]$$

donde $M = N / 2^l$ es la longitud de la secuencia en el nodo padre.

#### Conservación Estricta de Energía de Parseval
Dado que la matriz de transformación formada por los coeficientes de `db4` es unitaria y ortonormal, la energía se conserva estrictamente:

$$E_{\text{total}} = \sum_{n=0}^{4095} x^2[n] = \sum_{b=0}^{15} \sum_{m=0}^{255} w_b^2[m]$$

### 4.4 Inversión Espectral y Mapeo con Código Gray (Paley Ordering)

En la descomposición recursiva de paquetes wavelet surge un fenómeno físico de modulación:
* El filtrado pasa-altos traslada el rango $[\pi/2, \pi]$ a la banda base.
* El submuestreo por factor 2 produce un aliasing simétrico que **invierte el espectro** del resultado: las frecuencias más altas de la señal original terminan en las frecuencias más bajas del sub-nodo.

Por esta razón, si se leen los 16 nodos terminales en el orden secuencial del árbol binario, las sub-bandas resultantes **no** están ordenadas de forma creciente en frecuencia.

Para recuperar la linealidad espectral, se aplica la **permutación de Paley basada en el Código Gray inverso**:

$$\text{Nodo}(b) = \text{WPT\_GRAY\_MAP}[b] = \{0, 1, 3, 2, 6, 7, 5, 4, 12, 13, 15, 14, 10, 11, 9, 8\}$$

#### Tabla de Correspondencia Espectral:

| Sub-banda ($B_k$) | Frecuencia Física (Hz) | Nodo en Árbol WPT | Razón de Inversión |
|---|---|---|---|
| $B_0$ | $0 - 800\text{ Hz}$ | $0$ | Rama directa pasa-bajos |
| $B_1$ | $800 - 1600\text{ Hz}$ | $1$ | Rama directa |
| $B_2$ | $1600 - 2400\text{ Hz}$ | $3$ | Rama invertida por diezmado |
| $B_3$ | $2400 - 3200\text{ Hz}$ | $2$ | Rama invertida por diezmado |
| $B_4$ | $3200 - 4000\text{ Hz}$ | $6$ | Rama directa |
| $B_5$ | $4000 - 4800\text{ Hz}$ | $7$ | Rama directa |
| $B_6$ | $4800 - 5600\text{ Hz}$ | $5$ | Rama invertida |
| $B_7$ | $5600 - 6400\text{ Hz}$ | $4$ | Rama invertida |
| $B_8$ | $6400 - 7200\text{ Hz}$ | $12$ | Rama directa |
| $B_9$ | $7200 - 8000\text{ Hz}$ | $13$ | Rama directa |
| $B_{10}$ | $8000 - 8800\text{ Hz}$ | $15$ | Rama invertida |
| $B_{11}$ | $8800 - 9600\text{ Hz}$ | $14$ | Rama invertida |
| $B_{12}$ | $9600 - 10400\text{ Hz}$ | $10$ | Rama directa |
| $B_{13}$ | $10400 - 11200\text{ Hz}$ | $11$ | Rama directa |
| $B_{14}$ | $11200 - 12000\text{ Hz}$ | $9$ | Rama invertida |
| $B_{15}$ | $12000 - 12800\text{ Hz}$ | $8$ | Rama invertida |

### 4.5 Métricas de Sub-banda y Entropía de Shannon

Para cada sub-banda ordenada $b \in [0, 15]$:
1. **Energía de la Sub-banda:**
   $$E_b = \sum_{m=0}^{255} w_b^2[m], \quad E_{\text{total}} = \sum_{b=0}^{15} E_b$$
2. **Porcentaje de Energía y Amplitud Eficaz ($\text{RMS}$):**
   $$E_{\%}[b] = \left(\frac{E_b}{E_{\text{total}}}\right) \times 100\%, \quad \text{RMS}_{\text{mV}}[b] = \sqrt{\frac{E_b}{256}}$$
3. **Densidad Espectral de Energía:**
   $$\text{ESD}[b] = \frac{E_b}{\Delta f_{\text{WPT}}} = \frac{E_b}{800\text{ Hz}} \quad \left[\frac{\text{mV}^2}{\text{Hz}}\right]$$
4. **Entropía de Información de Shannon ($S_{\text{WPT}}$):**
   $$p_b = \frac{E_b}{E_{\text{total}}}, \quad S = -\sum_{b=0}^{15} p_b \ln(p_b)$$
   El valor máximo ocurre con energía equipartida ($p_b = 1/16$):
   $$S_{\max} = \ln(16) = 4\ln(2) \approx 2.7725887$$
   Se calcula la **Entropía Normalizada** $S_{\text{norm}} \in [0.0, 1.0]$:
   $$S_{\text{norm}} = \frac{S}{\ln(16)}$$
   * **Señal Tonal Estable:** Energía concentrada en una sola banda $\implies S_{\text{norm}} \approx 0.05 - 0.20$.
   * **Ruido Blanco / Falla Severa:** Energía dispersa uniformemente $\implies S_{\text{norm}} \approx 0.85 - 1.00$.

### 4.6 Detección y Localización de Transitorios en Alta Frecuencia

Para la detección de impactos mecánicos breves (fisuras en pistas de rodamientos o arcos eléctricos), la descomposición de Nivel 1 proporciona la máxima resolución temporal disponible:
* **Sub-espacio:** Detalle $D_1$ ($6.4\text{ kHz}$ a $12.8\text{ kHz}$).
* **Longitud:** 2048 coeficientes espaciados temporalmente a $\Delta t = 2 / f_s = 78.125\ \mu\text{s}$.

#### Algoritmo Estadístico de Disparo:
1. Se evalúa el nivel de dispersión basal de alta frecuencia:
   $$\sigma_{\text{HF}} = \sqrt{\frac{1}{2048} \sum_{n=0}^{2047} d_1^2[n]}$$
2. Se localiza el coeficiente pico absoluto y su índice:
   $$|d_1|_{\max} = \max_n |d_1[n]|, \quad n_{\max} = \arg\max_n |d_1[n]|$$
3. Se calcula el **Factor de Cresta en Alta Frecuencia**:
   $$\text{CF}_{\text{HF}} = \frac{|d_1|_{\max}}{\sigma_{\text{HF}}}$$
4. **Criterio de Decisión de Transitorio:**
   $$\text{Transitorio Activo} \iff (\text{CF}_{\text{HF}} > 4.5) \;\land\; (|d_1|_{\max} > 35.0\text{ mV})$$
5. **Localización Temporal Sub-milisegundo:**
   * Muestra de ocurrencia dentro del bloque de 4096: $n_{\text{sample}} = 2 \cdot n_{\max}$.
   * Offset temporal exacto desde el inicio del bloque:
     $$t_{\text{transitorio}} = \frac{n_{\text{sample}} \times 10^6}{f_s} \quad [\mu\text{s}]$$

---

## 5. Cuadro Comparativo de Capacidades y Sinergia

| Criterio | Motor FFT ([`dsp_engine.c`](file:///c:/Users/hrodriguez/Documents/Proyectos%20Personales/Vectech/I+D/3_DSP_Ethernet/01-h755-fft-udp/Firmware/Fft_Eth_H755/CM7/Src/dsp_engine.c)) | Motor Wavelet WPT ([`dsp_wavelet.c`](file:///c:/Users/hrodriguez/Documents/Proyectos%20Personales/Vectech/I+D/3_DSP_Ethernet/01-h755-fft-udp/Firmware/Fft_Eth_H755/CM7/Src/dsp_wavelet.c)) |
| :--- | :--- | :--- |
| **Transformada** | Real Fast Fourier Transform (Split-Radix) | Wavelet Packet Transform (Mallat filter bank) |
| **Función Base** | Sinusoides ortogonales ($e^{j\omega t}$) | Wavelet ortonormal Daubechies-4 (`db4`) |
| **Soporte Temporal de la Base** | Infinito (global al bloque) | Compacto (8 coeficientes en tiempo) |
| **Resolución Espectral** | Fina: $\Delta f = 6.25\text{ Hz}$ (512 bins hasta Nyquist) | Uniforme por bandas: $\Delta f = 800.0\text{ Hz}$ (16 bandas) |
| **Resolución Temporal** | Nula (no discrimina cuándo ocurrió el evento) | Excelente: $78.1\ \mu\text{s}$ en $D_1$ para localización |
| **Manejo de Fuga Espectral** | Ventana Blackman-Harris 4T ($-92\text{ dB}$) | Conservación exacta de energía de Parseval |
| **Métricas Clave Extraídas** | $f_0$ sub-bin, amplitud, armónicos $2f_0 \dots 10f_0$, THD, SNR | Distribución de energía $\%$, RMS por banda, Entropía de Shannon, Factor de Cresta HF |
| **Diagnóstico Típico** | Desbalance rotativo, desalineación, soltura mecánica, armónicos de red | Fisuras de rodamientos, picaduras de engranajes, arcos eléctricos, transitorios impulsivos |

---

## 6. Referencias Bibliográficas y Estándares

1. **Oppenheim, A. V., & Schafer, R. W.** (2009). *Discrete-Time Signal Processing* (3rd ed.). Prentice Hall.
2. **Harris, F. J.** (1978). *On the use of windows for harmonic analysis with the discrete Fourier transform*. Proceedings of the IEEE, 66(1), 51-83.
3. **Mallat, S.** (2008). *A Wavelet Tour of Signal Processing: The Sparse Way* (3rd ed.). Academic Press.
4. **Daubechies, I.** (1992). *Ten Lectures on Wavelets*. CBMS-NSF Regional Conference Series in Applied Mathematics, SIAM.
5. **ARM Limited.** (2023). *CMSIS-DSP Software Library: Transform Functions (`arm_rfft_fast_f32`, `arm_cmplx_mag_f32`)*.
