/**
 * Controlador de Interfaz de Usuario (UIController):
 * Actualiza métricas numéricas, barras, paneles Wavelet, armónicos y estados de señal.
 */
export class UIController {
  constructor() {
    // Header & Estado
    this.statusPill = document.getElementById('statusPill');
    this.statusText = document.getElementById('statusText');
    this.netRateEl = document.getElementById('netRate');

    // Banner de Señal
    this.signalBanner = document.getElementById('signalBanner');
    this.signalStateText = document.getElementById('signalStateText');
    this.blockIdEl = document.getElementById('blockId');

    // Métricas Principales
    this.f0Val = document.getElementById('f0Val');
    this.magVal = document.getElementById('magVal');
    this.magMeter = document.getElementById('magMeter');
    this.noiseVal = document.getElementById('noiseVal');
    this.snrVal = document.getElementById('snrVal');
    this.thdVal = document.getElementById('thdVal');
    this.thdMeter = document.getElementById('thdMeter');
    this.thdQuality = document.getElementById('thdQuality');

    // DC Offset y Vpp
    this.dcVal = document.getElementById('dcVal');
    this.dcVolt = document.getElementById('dcVolt');
    this.dcMeter = document.getElementById('dcMeter');
    this.vppVal = document.getElementById('vppVal');
    this.vppVolt = document.getElementById('vppVolt');
    this.vppMeter = document.getElementById('vppMeter');

    // Armónicos (H1 .. H10)
    this.harmonicsTotal = document.getElementById('harmonicsTotal');
    this.harmonicsElements = [];
    for (let i = 1; i <= 10; i++) {
      this.harmonicsElements.push({
        val: document.getElementById(`h${i}Val`),
        bar: document.getElementById(`h${i}Bar`)
      });
    }

    // Wavelet WPT 16 Bandas (B0 .. B15)
    this.wptBands = [];
    for (let i = 0; i < 16; i++) {
      this.wptBands.push({
        rms: document.getElementById(`b${i}Rms`),
        pct: document.getElementById(`b${i}Pct`),
        bar: document.getElementById(`b${i}Bar`)
      });
    }

    // Monitor de Transitorios
    this.transientBadge = document.getElementById('transientBadge');
    this.transientStatusText = document.getElementById('transientStatusText');
    this.transientCount = document.getElementById('transientCount');
    this.transientMag = document.getElementById('transientMag');
    this.transientSample = document.getElementById('transientSample');
    this.transientTime = document.getElementById('transientTime');
    this.d1Crest = document.getElementById('d1Crest');
    this.transientResetTimeout = null;

    // Entropía de Shannon
    this.entropyVal = document.getElementById('entropyVal');
    this.entropyMeter = document.getElementById('entropyMeter');
    this.entropyQuality = document.getElementById('entropyQuality');

    this.waveletDisplayMode = 'pct'; // 'pct' o 'density'
    this.lastWaveletData = null;
  }

  setConnected(isConnected) {
    if (!this.statusPill || !this.statusText) return;
    if (isConnected) {
      this.statusPill.classList.remove('offline');
      this.statusText.textContent = 'CONECTADO';
    } else {
      this.statusPill.classList.add('offline');
      this.statusText.textContent = 'RECONECTANDO';
    }
  }

  setNetRateText(refreshIntervalMs, isPaused = false) {
    if (!this.netRateEl) return;
    if (isPaused) {
      this.netRateEl.textContent = 'PAUSADO (HOLD)';
    } else {
      const rateSec = (refreshIntervalMs / 1000).toFixed(1);
      this.netRateEl.textContent = `6.25 Hz UDP | UI: ${rateSec}s | Scalo: 60 FPS`;
    }
  }

  toggleWaveletDisplayMode() {
    this.waveletDisplayMode = (this.waveletDisplayMode === 'pct') ? 'density' : 'pct';
    if (this.lastWaveletData) {
      this.updateWaveletUI(this.lastWaveletData);
    }
    return this.waveletDisplayMode;
  }

  updateTelemetry(data) {
    if (!data) return;

    if (this.blockIdEl) this.blockIdEl.textContent = data.block_id;

    // Banner de detección
    if (this.signalBanner && this.signalStateText) {
      if (data.detected) {
        this.signalBanner.className = 'banner detected';
        this.signalStateText.textContent = 'SEÑAL PERIÓDICA';
      } else {
        this.signalBanner.className = 'banner noise';
        this.signalStateText.textContent = 'RUIDO / SIN SEÑAL';
      }
    }

    // Métricas principales
    if (this.f0Val) this.f0Val.textContent = data.f0_hz ? data.f0_hz.toFixed(1) : '0.0';
    if (this.magVal) this.magVal.textContent = data.f0_mag_mv || 0;
    if (this.noiseVal) this.noiseVal.textContent = data.noise_floor_mv || 0;
    if (this.snrVal) this.snrVal.textContent = data.snr_db ? data.snr_db.toFixed(1) : '0.0';
    if (this.thdVal) this.thdVal.textContent = data.thd_pct ? data.thd_pct.toFixed(1) : '0.0';

    // Barra de amplitud (escala 0-1500 mV)
    if (this.magMeter) {
      const magPct = Math.min(100, ((data.f0_mag_mv || 0) / 1500) * 100);
      this.magMeter.style.width = magPct + '%';
    }

    // Offset DC y Vpp
    if (this.dcVal && data.dc_offset_mv !== undefined) {
      this.dcVal.textContent = data.dc_offset_mv;
      if (this.dcVolt) this.dcVolt.textContent = (data.dc_offset_mv / 1000.0).toFixed(2);
      if (this.dcMeter) this.dcMeter.style.width = Math.min(100, (data.dc_offset_mv / 3300.0) * 100) + '%';
    }

    if (this.vppVal && data.v_pp_mv !== undefined) {
      this.vppVal.textContent = data.v_pp_mv;
      if (this.vppVolt) this.vppVolt.textContent = (data.v_pp_mv / 1000.0).toFixed(2);
      if (this.vppMeter) this.vppMeter.style.width = Math.min(100, (data.v_pp_mv / 3300.0) * 100) + '%';
    }

    // Calidad y barra de THD
    const thd = data.thd_pct || 0;
    const thdPct = Math.min(100, thd);
    if (this.thdMeter) this.thdMeter.style.width = thdPct + '%';
    if (this.thdQuality && this.thdMeter) {
      if (thd < 5.0) {
        this.thdMeter.style.background = 'var(--accent-emerald)';
        this.thdQuality.textContent = 'Baja distorsión (Excelente)';
        this.thdQuality.style.color = 'var(--accent-emerald)';
      } else if (thd < 20.0) {
        this.thdMeter.style.background = 'var(--accent-amber)';
        this.thdQuality.textContent = 'Distorsión moderada';
        this.thdQuality.style.color = 'var(--accent-amber)';
      } else {
        this.thdMeter.style.background = 'var(--accent-rose)';
        this.thdQuality.textContent = 'Alta distorsión armónica';
        this.thdQuality.style.color = 'var(--accent-rose)';
      }
    }

    // Armónicos (1 a 10)
    const harms = data.harmonics || [];
    let maxHarm = 1;
    let harmTotal = 0;
    for (let i = 0; i < harms.length; i++) {
      if (harms[i] > maxHarm) maxHarm = harms[i];
      if (i > 0) harmTotal += harms[i];
    }
    if (this.harmonicsTotal) {
      this.harmonicsTotal.textContent = `Total armónicos: ${harmTotal} mV`;
    }

    for (let i = 0; i < this.harmonicsElements.length; i++) {
      if (i < harms.length) {
        const val = harms[i];
        if (this.harmonicsElements[i].val) this.harmonicsElements[i].val.textContent = val;
        const pct = Math.min(100, (val / maxHarm) * 100);
        if (this.harmonicsElements[i].bar) this.harmonicsElements[i].bar.style.height = pct + '%';
      }
    }

    // Métricas Wavelet
    if (data.wavelet) {
      this.lastWaveletData = data.wavelet;
      this.updateWaveletUI(data.wavelet);
    }
  }

  updateWaveletUI(w) {
    if (!w) return;

    const numBands = w.num_bands || (w.energy_pct ? w.energy_pct.length : 16);
    const isDens = (this.waveletDisplayMode === 'density');
    const dens = w.energy_density || new Array(numBands).fill(0);
    const maxDens = Math.max(0.01, ...dens);

    for (let i = 0; i < 16; i++) {
      if (!this.wptBands[i]) continue;
      if (i < numBands) {
        const pct = (w.energy_pct && w.energy_pct[i] !== undefined) ? w.energy_pct[i] : 0;
        const rms = (w.rms_mv && w.rms_mv[i] !== undefined) ? w.rms_mv[i] : 0;
        const dVal = (dens[i] !== undefined) ? dens[i] : 0;

        if (this.wptBands[i].rms) this.wptBands[i].rms.textContent = rms + ' mV';

        if (isDens) {
          if (this.wptBands[i].pct) this.wptBands[i].pct.textContent = dVal.toFixed(1) + ' mV²/Hz';
          const wPct = Math.min(100, Math.max(0, (dVal / maxDens) * 100));
          if (this.wptBands[i].bar) this.wptBands[i].bar.style.width = wPct + '%';
        } else {
          if (this.wptBands[i].pct) this.wptBands[i].pct.textContent = pct.toFixed(1) + '%';
          if (this.wptBands[i].bar) this.wptBands[i].bar.style.width = Math.min(100, Math.max(0, pct)) + '%';
        }
      }
    }

    // Transitorios y anomalías
    if (this.transientCount) this.transientCount.textContent = w.transient_count || 0;
    if (this.d1Crest) this.d1Crest.textContent = (w.crest_factor !== undefined ? w.crest_factor.toFixed(1) : '0.0') + 'x';

    if (w.transient_detected) {
      if (this.transientBadge) {
        this.transientBadge.className = 'transient-badge alert';
        this.transientStatusText.textContent = '⚠️ TRANSITORIO DETECTADO';
      }
      if (this.transientMag) this.transientMag.textContent = w.transient_mag_mv || 0;
      if (this.transientSample) this.transientSample.innerHTML = `#${w.transient_sample || 0} <small style="font-size:0.65rem; color:var(--text-muted);">/ 4096</small>`;
      if (this.transientTime) this.transientTime.innerHTML = `${((w.transient_time_us || 0) / 1000).toFixed(2)} <small style="font-size:0.7rem; color:var(--text-muted);">ms</small>`;

      if (this.transientResetTimeout) clearTimeout(this.transientResetTimeout);
      this.transientResetTimeout = setTimeout(() => {
        if (this.transientBadge) {
          this.transientBadge.className = 'transient-badge normal';
          this.transientStatusText.textContent = 'SISTEMA ESTABLE / SIN ANOMALÍAS';
        }
      }, 1500);
    } else if (!this.transientResetTimeout) {
      if (this.transientBadge && this.transientBadge.className !== 'transient-badge normal') {
        this.transientBadge.className = 'transient-badge normal';
        this.transientStatusText.textContent = 'SISTEMA ESTABLE / SIN ANOMALÍAS';
      }
    }

    // Entropía de Shannon Wavelet (normalizada a ln(16))
    if (this.entropyVal) {
      const sNorm = (w.entropy_norm !== undefined) ? w.entropy_norm : ((w.entropy_shannon || 0) / Math.log(16));
      const sNormPct = Math.min(100, Math.max(0, sNorm * 100));
      const sVal = (w.entropy_shannon !== undefined) ? w.entropy_shannon : 0;

      this.entropyVal.innerHTML = `${sVal.toFixed(2)} <small style="font-size:0.7rem; color:var(--text-muted);">(${sNormPct.toFixed(0)}%)</small>`;
      if (this.entropyMeter) {
        this.entropyMeter.style.width = `${sNormPct}%`;
        if (sNormPct < 35) {
          this.entropyMeter.style.background = 'var(--accent-emerald)';
          if (this.entropyQuality) {
            this.entropyQuality.textContent = 'Mono-tono / Concentrada (Baja entropía)';
            this.entropyQuality.style.color = 'var(--accent-emerald)';
          }
        } else if (sNormPct < 75) {
          this.entropyMeter.style.background = 'var(--accent-cyan)';
          if (this.entropyQuality) {
            this.entropyQuality.textContent = 'Multicomponente armónico (Moderada)';
            this.entropyQuality.style.color = 'var(--accent-cyan)';
          }
        } else {
          this.entropyMeter.style.background = 'var(--accent-rose)';
          if (this.entropyQuality) {
            this.entropyQuality.textContent = 'Ruido / Transitorio caótico (Alta entropía)';
            this.entropyQuality.style.color = 'var(--accent-rose)';
          }
        }
      }
    }
  }
}
