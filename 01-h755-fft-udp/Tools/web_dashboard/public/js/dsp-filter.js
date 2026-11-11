/**
 * Filtro de Media Móvil Exponencial (EMA) para suavizado de telemetría DSP.
 */
export class DSPFilter {
  constructor(alpha = 0.35) {
    this.alpha = alpha;
    this.smoothed = null;
  }

  reset() {
    this.smoothed = null;
  }

  apply(raw) {
    if (!this.smoothed) {
      const nBands = (raw.wavelet && raw.wavelet.energy_pct) ? raw.wavelet.energy_pct.length : 16;
      this.smoothed = {
        f0_hz: raw.f0_hz,
        f0_mag_mv: raw.f0_mag_mv,
        noise_floor_mv: raw.noise_floor_mv,
        snr_db: raw.snr_db,
        thd_pct: raw.thd_pct,
        dc_offset_mv: raw.dc_offset_mv || 0,
        v_pp_mv: raw.v_pp_mv || 0,
        harmonics: [...(raw.harmonics || new Array(10).fill(0))],
        wavelet: raw.wavelet ? {
          energy_pct: [...raw.wavelet.energy_pct],
          rms_mv: [...raw.wavelet.rms_mv],
          energy_density: raw.wavelet.energy_density ? [...raw.wavelet.energy_density] : new Array(nBands).fill(0)
        } : null
      };
      return raw;
    }

    const s = this.smoothed;
    const a = this.alpha;

    s.f0_hz += a * (raw.f0_hz - s.f0_hz);
    s.f0_mag_mv += a * (raw.f0_mag_mv - s.f0_mag_mv);
    s.noise_floor_mv += a * (raw.noise_floor_mv - s.noise_floor_mv);
    s.snr_db += a * (raw.snr_db - s.snr_db);
    s.thd_pct += a * (raw.thd_pct - s.thd_pct);
    s.dc_offset_mv += a * ((raw.dc_offset_mv || 0) - s.dc_offset_mv);
    s.v_pp_mv += a * ((raw.v_pp_mv || 0) - s.v_pp_mv);

    const hCount = raw.harmonics ? raw.harmonics.length : 10;
    s.harmonics = s.harmonics || [];
    for (let i = 0; i < hCount; i++) {
      const rawH = (raw.harmonics && raw.harmonics[i] !== undefined) ? raw.harmonics[i] : 0;
      const curH = s.harmonics[i] !== undefined ? s.harmonics[i] : rawH;
      s.harmonics[i] = curH + a * (rawH - curH);
    }

    if (raw.wavelet) {
      const nBands = raw.wavelet.num_bands || (raw.wavelet.energy_pct ? raw.wavelet.energy_pct.length : 16);
      if (!s.wavelet || !s.wavelet.energy_pct || s.wavelet.energy_pct.length !== nBands) {
        s.wavelet = {
          energy_pct: [...raw.wavelet.energy_pct],
          rms_mv: [...raw.wavelet.rms_mv],
          energy_density: raw.wavelet.energy_density ? [...raw.wavelet.energy_density] : new Array(nBands).fill(0)
        };
      } else {
        for (let i = 0; i < nBands; i++) {
          const rawP = raw.wavelet.energy_pct[i] || 0;
          const rawR = raw.wavelet.rms_mv[i] || 0;
          const rawD = (raw.wavelet.energy_density && raw.wavelet.energy_density[i] !== undefined) ? raw.wavelet.energy_density[i] : 0;
          s.wavelet.energy_pct[i] += a * (rawP - s.wavelet.energy_pct[i]);
          s.wavelet.rms_mv[i] += a * (rawR - s.wavelet.rms_mv[i]);
          if (s.wavelet.energy_density) {
            const curD = s.wavelet.energy_density[i] || 0;
            s.wavelet.energy_density[i] = curD + a * (rawD - curD);
          }
        }
      }
    }

    return {
      ...raw,
      f0_hz: s.f0_hz,
      f0_mag_mv: Math.round(s.f0_mag_mv),
      noise_floor_mv: Math.round(s.noise_floor_mv),
      snr_db: s.snr_db,
      thd_pct: s.thd_pct,
      dc_offset_mv: Math.round(s.dc_offset_mv),
      v_pp_mv: Math.round(s.v_pp_mv),
      harmonics: s.harmonics.map(v => Math.round(v)),
      wavelet: raw.wavelet ? {
        ...raw.wavelet,
        energy_pct: s.wavelet ? s.wavelet.energy_pct : raw.wavelet.energy_pct,
        rms_mv: s.wavelet ? s.wavelet.rms_mv.map(v => Math.round(v)) : raw.wavelet.rms_mv,
        energy_density: s.wavelet && s.wavelet.energy_density ? s.wavelet.energy_density : raw.wavelet.energy_density
      } : null
    };
  }
}
