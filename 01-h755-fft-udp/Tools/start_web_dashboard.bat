@echo off
title STM32H755 FFT Web Dashboard
cd /d "%~dp0web_dashboard"
node server.js
pause
