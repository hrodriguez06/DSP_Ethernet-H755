@echo off
title STM32H755 Dual-Core Firmware Flasher
powershell -ExecutionPolicy Bypass -File "%~dp0flash_firmware.ps1"
pause
