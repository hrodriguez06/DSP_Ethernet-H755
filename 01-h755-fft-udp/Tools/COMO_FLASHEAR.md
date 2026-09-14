# Guía de Programación y Flasheo Dual-Core (STM32H755ZI-Q)

Esta guía describe los tres métodos disponibles para volver a cargar el firmware en la tarjeta **NUCLEO-H755ZI-Q** (Cortex-M7 + Cortex-M4) correspondiente al proyecto `01-h755-fft-udp`.

---

## 1. Mapa de Memoria Flash Dual-Core

El microcontrolador STM32H755 dispone de **2 MB de memoria Flash dividida en dos bancos físicos independientes** de 1 MB cada uno. Cada núcleo debe programarse en su respectivo banco según sus scripts de enlace (`.ld`):

| Núcleo | Función en el Proyecto | Banco Físico | Dirección Base | Archivo ELF |
|:---|:---|:---:|:---:|:---|
| **Cortex-M7 (CM7)** | DSP (WPT + FFT), LwIP Ethernet, Servidor UDP | **Banco 1** | `0x08000000` | `Firmware/Fft_Eth_H755/CM7/Debug/Fft_Eth_H755_CM7.elf` |
| **Cortex-M4 (CM4)** | Adquisición ADC1 con DMA1 Circular a 25.6 kHz | **Banco 2** | `0x08100000` | `Firmware/Fft_Eth_H755/CM4/Debug/Fft_Eth_H755_CM4.elf` |

---

## Método 1: Doble Clic (Recomendado y Rápido)

Se incluye un script automatizado en esta carpeta (`Tools/`) que detecta el programador ST-LINK y carga ambos núcleos en un solo paso:

1. Conecta la placa **NUCLEO-H755ZI-Q** al puerto USB de la PC mediante el conector micro-USB del ST-LINK (CN1).
2. Haz **doble clic** sobre:
   - **`flash_firmware.bat`**
3. El script se encargará automáticamente de:
   - Localizar el ejecutable `STM32_Programmer_CLI.exe` en la instalación de STM32CubeIDE o STM32CubeProgrammer.
   - Flashear el **Cortex-M7** en el Banco 1 (`0x08000000`).
   - Flashear el **Cortex-M4** en el Banco 2 (`0x08100000`).
   - Verificar la integridad de la memoria grabada.
   - Reiniciar el hardware (`-s`) para que ambos procesadores comiencen su ejecución inmediatamente.

---

## Método 2: Desde la Consola de Comandos (PowerShell)

### Opción A: Usando el script auxiliar de PowerShell
Desde una terminal de PowerShell ubicada en la carpeta raíz del proyecto (`01-h755-fft-udp`):

```powershell
powershell -ExecutionPolicy Bypass -File "Tools\flash_firmware.ps1"
```

### Opción B: Invocación directa de `STM32_Programmer_CLI.exe`
Si deseas ejecutar directamente la herramienta oficial de STMicroelectronics:

```powershell
# 1. Definir ruta al programador CLI (ajustar si tu version de CubeIDE varia)
$cli = "C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.500.202603051304\tools\bin\STM32_Programmer_CLI.exe"

# 2. Rutas a los binarios compilados
$cm7 = "Firmware\Fft_Eth_H755\CM7\Debug\Fft_Eth_H755_CM7.elf"
$cm4 = "Firmware\Fft_Eth_H755\CM4\Debug\Fft_Eth_H755_CM4.elf"

# 3. Flasheo simultaneo Under Reset (UR) con verificacion (-v) y arranque (-s)
& $cli -c port=SWD mode=UR -d "$cm7" -v -d "$cm4" -v -s
```

---

## Método 3: Desde la Interfaz Gráfica de STM32CubeIDE

Si estás realizando modificaciones en el código fuente en C dentro del entorno de desarrollo:

### Paso 1: Recompilar ambos proyectos (si hubo cambios en código)
1. En la barra superior, pulsa la flecha junto al martillo (**Build**) y selecciona **Debug** para el proyecto raíz o ambos núcleos.

### Paso 2: Flashear Cortex-M7
1. En el panel **Project Explorer**, haz clic derecho sobre el proyecto **`Fft_Eth_H755_CM7`**.
2. Selecciona **Run As** $\rightarrow$ **1 STM32 C/C++ Application**.
3. El entorno programará el Banco 1 de la memoria Flash.

### Paso 3: Flashear Cortex-M4
1. En el panel **Project Explorer**, haz clic derecho sobre el proyecto **`Fft_Eth_H755_CM4`**.
2. Selecciona **Run As** $\rightarrow$ **1 STM32 C/C++ Application**.
3. El entorno programará el Banco 2 de la memoria Flash.

### Paso 4: Sincronización Inicial
1. Pulsa el **botón negro (RESET)** en la tarjeta NUCLEO-H755ZI-Q para garantizar que ambos procesadores comiencen su secuencia de arranque sincronizada desde el inicio.

---

## Resolución de Problemas Frecuentes

- **Error `ST-LINK error (DEV_TARGET_NOT_HALTED)` o no se detecta la placa:**
  - Asegúrate de que el parámetro de conexión sea `mode=UR` (Connect Under Reset), el cual está configurado por defecto en los scripts provistos.
  - Verifica que el cable USB esté conectado al conector ST-LINK (CN1) y que el LED de estado (COM) esté iluminado.
- **La red Ethernet no responde tras flashear:**
  - Tras programar la tarjeta, espera unos 3 a 5 segundos para que la pila LwIP negocie el enlace físico (PHY LAN8742) y configure su dirección estática `192.168.1.172`.
