# ==============================================================================
# Script para Flashear STM32H755 Dual-Core (CM7 + CM4)
# Proyecto: 3_DSP_Ethernet / 01-h755-fft-udp
# ==============================================================================

$ErrorActionPreference = "Stop"

Write-Host "==================================================================" -ForegroundColor Cyan
Write-Host "  PROGRAMADOR DUAL-CORE STM32H755ZI-Q (Cortex-M7 + Cortex-M4)     " -ForegroundColor Cyan
Write-Host "==================================================================" -ForegroundColor Cyan

# 1. Buscar STM32_Programmer_CLI
$cliCandidates = @(
    "STM32_Programmer_CLI.exe",
    "C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.500.202603051304\tools\bin\STM32_Programmer_CLI.exe",
    "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
)

$cliPath = $null
foreach ($cand in $cliCandidates) {
    if (Get-Command $cand -ErrorAction SilentlyContinue) {
        $cliPath = (Get-Command $cand).Source
        break
    } elseif (Test-Path $cand) {
        $cliPath = $cand
        break
    }
}

if (-not $cliPath) {
    Write-Host "[BUSCANDO] Buscando STM32_Programmer_CLI en C:\ST..." -ForegroundColor Yellow
    $found = Get-ChildItem -Path "C:\ST" -Filter "STM32_Programmer_CLI.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { $cliPath = $found.FullName }
}

if (-not $cliPath) {
    Write-Host "[ERROR] No se encontro STM32_Programmer_CLI.exe en el sistema." -ForegroundColor Red
    Write-Host "Instala STM32CubeProgrammer o verifica la instalacion de STM32CubeIDE." -ForegroundColor Yellow
    Exit 1
}

Write-Host "[OK] Programador CLI: $cliPath" -ForegroundColor Green

# 2. Rutas a los binarios ELF
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir
$cm7Elf = Join-Path $projectRoot "Firmware\Fft_Eth_H755\CM7\Debug\Fft_Eth_H755_CM7.elf"
$cm4Elf = Join-Path $projectRoot "Firmware\Fft_Eth_H755\CM4\Debug\Fft_Eth_H755_CM4.elf"

if (-not (Test-Path $cm7Elf)) {
    Write-Host "[ERROR] No existe el binario CM7: $cm7Elf" -ForegroundColor Red
    Exit 1
}
if (-not (Test-Path $cm4Elf)) {
    Write-Host "[ERROR] No existe el binario CM4: $cm4Elf" -ForegroundColor Red
    Exit 1
}

Write-Host "[OK] CM7 ELF: $cm7Elf" -ForegroundColor Gray
Write-Host "[OK] CM4 ELF: $cm4Elf" -ForegroundColor Gray
Write-Host ""
Write-Host "[INFO] Conectando a la tarjeta NUCLEO-H755 via ST-LINK SWD (Under Reset)..." -ForegroundColor Yellow

# 3. Flasheo simultaneo de ambos bancos de Flash:
#    Bank 1 (0x08000000) <- CM7
#    Bank 2 (0x08100000) <- CM4
& $cliPath -c port=SWD mode=UR -d "$cm7Elf" -v -d "$cm4Elf" -v -s

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "==================================================================" -ForegroundColor Green
    Write-Host "  FIRMWARE DUAL-CORE CARGADO Y VERIFICADO EXITOSAMENTE!           " -ForegroundColor Green
    Write-Host "  Cortex-M7 y Cortex-M4 ejecutandose en la tarjeta Nucleo.        " -ForegroundColor Green
    Write-Host "==================================================================" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "[ERROR] Fallo el proceso de flasheo. Codigo de salida: $LASTEXITCODE" -ForegroundColor Red
    Exit $LASTEXITCODE
}
