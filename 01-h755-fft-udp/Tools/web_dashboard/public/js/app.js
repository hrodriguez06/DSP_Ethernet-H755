/**
 * Aplicación Principal del Dashboard STM32H755 FFT & DSP
 * Orquestador de módulos: Filtro DSP, Osciloscopio, Espectro, Escalograma, UI y SSE.
 */
import { DSPFilter } from './dsp-filter.js';
import { OscilloscopeRenderer } from './oscilloscope.js';
import { SpectrumRenderer } from './spectrum.js';
import { ScalogramRenderer } from './scalogram.js';
import { UIController } from './ui-controller.js';
import { SSEClient } from './sse-client.js';

// Instancias de módulos
const dspFilter = new DSPFilter(0.35);
const ui = new UIController();
const oscilloscope = new OscilloscopeRenderer('scopeCanvas', 'scopeBox', 'scopeDcVal', 'scopeVppVal');
const spectrum = new SpectrumRenderer('spectrumCanvas', 'canvasBox', 'specPeakInfo');
const scalogram = new ScalogramRenderer('scalogramCanvas', 'scalogramBox', 'scaloFpsBadge');
const sse = new SSEClient('/api/events');

// Estado de la aplicación
let refreshIntervalMs = 500; // 0.5s por defecto
let isPaused = false;
let enableSmoothing = true;
let lastUiTime = 0;
let lastTelemetryData = null;

// Controles de UI
const rateButtons = document.querySelectorAll('#rateButtons .rate-btn');
const btnSmoothing = document.getElementById('btnSmoothing');
const btnHold = document.getElementById('btnHold');
const btnWaveletMode = document.getElementById('btnWaveletMode');

// Handlers de botones de tasa de actualización
rateButtons.forEach(btn => {
  btn.addEventListener('click', () => {
    rateButtons.forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    refreshIntervalMs = parseInt(btn.dataset.rate, 10);
    ui.setNetRateText(refreshIntervalMs, isPaused);
  });
});

// Handler de filtro de suavizado (EMA)
if (btnSmoothing) {
  btnSmoothing.addEventListener('click', () => {
    enableSmoothing = !enableSmoothing;
    if (enableSmoothing) {
      btnSmoothing.classList.add('active');
      btnSmoothing.textContent = 'Filtro: ON';
    } else {
      btnSmoothing.classList.remove('active');
      btnSmoothing.textContent = 'Filtro: OFF';
      dspFilter.reset();
    }
  });
}

// Handler de Pausar / Reanudar (HOLD)
if (btnHold) {
  btnHold.addEventListener('click', () => {
    isPaused = !isPaused;
    scalogram.setPaused(isPaused);

    if (isPaused) {
      btnHold.classList.add('paused');
      btnHold.textContent = '▶ Reanudar';
      ui.setNetRateText(refreshIntervalMs, true);
    } else {
      btnHold.classList.remove('paused');
      btnHold.textContent = '⏸ Pausar';
      lastUiTime = 0; // Forzar actualización en el siguiente paquete
      ui.setNetRateText(refreshIntervalMs, false);
    }
  });
}

// Handler de modo de visualización Wavelet (% Energía vs Densidad)
if (btnWaveletMode) {
  btnWaveletMode.addEventListener('click', () => {
    const newMode = ui.toggleWaveletDisplayMode();
    btnWaveletMode.textContent = (newMode === 'pct') ? 'Modo: % Energía' : 'Modo: Densidad (mV²/Hz)';
  });
}

// Configurar SSE
sse.onOpen = () => {
  ui.setConnected(true);
};

sse.onError = () => {
  ui.setConnected(false);
};

sse.onTelemetry = (raw) => {
  if (isPaused) return;

  // Alimentar inmediatamente el motor de animación a 60 Hz del escalograma
  if (raw.wavelet && raw.wavelet.energy_pct) {
    scalogram.setTargetBands(raw.wavelet.energy_pct);
  }

  const dataToDisplay = enableSmoothing ? dspFilter.apply(raw) : raw;
  lastTelemetryData = dataToDisplay;

  const now = performance.now();
  if (now - lastUiTime >= refreshIntervalMs) {
    lastUiTime = now;

    // Actualizar métricas de UI
    ui.updateTelemetry(dataToDisplay);

    // Calcular bin pico fundamental
    if (dataToDisplay.f0_hz > 0) {
      const totalBins = spectrum.currentBins.length || 1024;
      const df = (dataToDisplay.n_fft && dataToDisplay.fs_hz)
        ? (dataToDisplay.fs_hz / dataToDisplay.n_fft)
        : (12800.0 / totalBins);
      const peakBin = Math.round(dataToDisplay.f0_hz / df);
      spectrum.setPeakIndex(peakBin);
    } else {
      spectrum.setPeakIndex(0);
    }

    // Renderizar osciloscopio y espectro
    oscilloscope.render(dataToDisplay);
    spectrum.render();
  }
};

sse.onSpectrum = (data) => {
  if (isPaused) return;
  if (data.bins) {
    spectrum.setBins(data.bins);
    spectrum.render();
  }
};

// Ajuste responsive de los lienzos Canvas
function resizeAll() {
  spectrum.resize();
  oscilloscope.resize();
  scalogram.resize();
}

window.addEventListener('resize', resizeAll);

// Inicialización de la aplicación al cargar el DOM
window.addEventListener('DOMContentLoaded', () => {
  ui.setNetRateText(refreshIntervalMs, isPaused);
  resizeAll();
  sse.connect();
  scalogram.start();
});
