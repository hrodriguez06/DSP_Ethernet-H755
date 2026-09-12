const http = require('http');
const dgram = require('dgram');
const fs = require('fs');
const path = require('path');
const os = require('os');

// Configuracion por defecto
const HTTP_PORT = parseInt(process.env.PORT || '8088', 10);
const BOARD_IP = process.env.BOARD_IP || '192.168.1.172';
const BOARD_PORT = parseInt(process.env.BOARD_PORT || '5002', 10);

const UDP_MAGIC_TELEMETRY = 0x44535032; // "DSP2"
const UDP_MAGIC_SPECTRUM  = 0x53504543; // "SPEC"

// Clientes SSE conectados
const sseClients = new Set();

// Socket UDP para comunicarse con la tarjeta Nucleo-H755
const udpSocket = dgram.createSocket('udp4');

udpSocket.on('error', (err) => {
  console.error(`[UDP] Error: ${err.message}`);
});

let lastTelemetry = null;
let lastSpectrum = null;
let partialSpectrum = { block_id: 0, bins: new Array(1024).fill(0), receivedParts: new Set() };

udpSocket.on('message', (msg, rinfo) => {
  if (msg.length >= 60 && msg.length <= 256) {
    const magic = msg.readUInt32LE(0);
    if (magic === UDP_MAGIC_TELEMETRY) {
      const block_id = msg.readUInt32LE(4);
      const timestamp_ms = msg.readUInt32LE(8);
      const fs_hz = msg.readUInt32LE(12);
      const n_fft = msg.readUInt32LE(16);
      const f0_hz_x10 = msg.readUInt32LE(20);
      const f0_mag_mv = msg.readUInt32LE(24);
      const noise_floor_mv = msg.readUInt32LE(28);
      const snr_db_x10 = msg.readUInt32LE(32);
      const thd_pct_x10 = msg.readUInt32LE(36);
      const flags = msg.readUInt32LE(40);

      const harmCount = (msg.length >= 70) ? 10 : 5;
      const harmonics = [];
      for (let i = 0; i < harmCount; i++) {
        harmonics.push(msg.readUInt16LE(44 + i * 2));
      }
      const dc_offset_mv = msg.readUInt16LE(44 + harmCount * 2);
      const v_pp_mv = msg.readUInt16LE(44 + harmCount * 2 + 2);

      // Metricas Wavelet (WPT 16 bandas si >= 144 B, o DWT 5 bandas si >= 96 B)
      let wavelet = null;
      if (msg.length >= 144) {
        const offset = 68;
        const energy_pct = [];
        for (let i = 0; i < 16; i++) {
          energy_pct.push(msg.readUInt16LE(offset + i * 2) / 10.0);
        }
        const rms_mv = [];
        for (let i = 0; i < 16; i++) {
          rms_mv.push(msg.readUInt16LE(offset + 32 + i * 2));
        }
        const transient_mag_mv = msg.readUInt16LE(offset + 64);
        const transient_sample = msg.readUInt16LE(offset + 66);
        const transient_count = msg.readUInt16LE(offset + 68);
        const crest_factor = msg.readUInt16LE(offset + 70) / 10.0;
        const entropy_norm = msg.readUInt16LE(offset + 72) / 1000.0;
        const transient_detected = (flags & 0x10) !== 0;

        const delta_f = 800.0; // 800 Hz constantes para las 16 sub-bandas WPT
        const packet_len = (n_fft || 4096) / 16; // 256 muestras
        const energy_density = [];
        let total_energy = 0;
        for (let i = 0; i < 16; i++) {
          const e = (rms_mv[i] * rms_mv[i]) * packet_len;
          total_energy += e;
          energy_density.push(Math.round(e / delta_f));
        }

        let entropy_shannon = 0;
        if (total_energy > 0) {
          for (let i = 0; i < 16; i++) {
            const p = energy_pct[i] / 100.0;
            if (p > 1e-5) {
              entropy_shannon -= p * Math.log(p);
            }
          }
        }

        wavelet = {
          mode: 'WPT',
          num_bands: 16,
          energy_pct,     // [B0 .. B15] %
          rms_mv,         // [B0 .. B15] mV
          energy_density, // [B0 .. B15] mV^2/Hz
          entropy_shannon: parseFloat(entropy_shannon.toFixed(3)),
          entropy_norm: parseFloat(entropy_norm.toFixed(3)),
          transient_detected,
          transient_mag_mv,
          transient_sample,
          transient_time_us: Math.round((transient_sample * 1000000) / (fs_hz || 25600)),
          transient_count,
          crest_factor
        };
      } else if (msg.length >= 96) {
        const offset = 68;
        const energy_pct = [
          msg.readUInt16LE(offset + 0) / 10.0,
          msg.readUInt16LE(offset + 2) / 10.0,
          msg.readUInt16LE(offset + 4) / 10.0,
          msg.readUInt16LE(offset + 6) / 10.0,
          msg.readUInt16LE(offset + 8) / 10.0
        ];
        const rms_mv = [
          msg.readUInt16LE(offset + 10),
          msg.readUInt16LE(offset + 12),
          msg.readUInt16LE(offset + 14),
          msg.readUInt16LE(offset + 16),
          msg.readUInt16LE(offset + 18)
        ];
        const transient_mag_mv = msg.readUInt16LE(offset + 20);
        const transient_sample = msg.readUInt16LE(offset + 22);
        const transient_count = msg.readUInt16LE(offset + 24);
        const crest_factor = msg.readUInt16LE(offset + 26) / 10.0;
        const transient_detected = (flags & 0x10) !== 0;

        const delta_f = [
          (fs_hz || 25600) / 4.0,  // D1: 6400 Hz
          (fs_hz || 25600) / 8.0,  // D2: 3200 Hz
          (fs_hz || 25600) / 16.0, // D3: 1600 Hz
          (fs_hz || 25600) / 32.0, // D4: 800 Hz
          (fs_hz || 25600) / 32.0  // A4: 800 Hz
        ];
        const band_lengths = [1024, 512, 256, 128, 128];
        const energy_density = [];
        let total_energy = 0;
        for (let i = 0; i < 5; i++) {
          const e = (rms_mv[i] * rms_mv[i]) * band_lengths[i];
          total_energy += e;
          energy_density.push(Math.round(e / delta_f[i]));
        }

        let entropy_shannon = 0;
        if (total_energy > 0) {
          for (let i = 0; i < 5; i++) {
            const p = energy_pct[i] / 100.0;
            if (p > 1e-5) {
              entropy_shannon -= p * Math.log(p);
            }
          }
        }
        const entropy_norm = Math.min(1.0, Math.max(0.0, entropy_shannon / Math.log(5)));

        wavelet = {
          mode: 'DWT',
          num_bands: 5,
          energy_pct,     // [D1, D2, D3, D4, A4] %
          rms_mv,         // [D1, D2, D3, D4, A4] mV
          energy_density, // [D1, D2, D3, D4, A4] mV^2/Hz
          entropy_shannon: parseFloat(entropy_shannon.toFixed(3)),
          entropy_norm: parseFloat(entropy_norm.toFixed(3)),
          transient_detected,
          transient_mag_mv,
          transient_sample,
          transient_time_us: Math.round((transient_sample * 1000000) / (fs_hz || 25600)),
          transient_count,
          crest_factor
        };
      }

      const payload = {
        type: 'telemetry',
        block_id,
        timestamp_ms,
        fs_hz,
        n_fft,
        f0_hz: (f0_hz_x10 / 10.0),
        f0_mag_mv,
        dc_offset_mv,
        v_pp_mv,
        noise_floor_mv,
        snr_db: (snr_db_x10 / 10.0),
        thd_pct: (thd_pct_x10 / 10.0),
        flags,
        detected: (flags & 0x02) !== 0,
        harmonics,
        wavelet
      };

      lastTelemetry = payload;
      broadcastSSE('telemetry', payload);
    }
  } else if (msg.length === 1036) {
    const magic = msg.readUInt32LE(0);
    if (magic === UDP_MAGIC_SPECTRUM) {
      const block_id = msg.readUInt32LE(4);
      const part = msg.readUInt16LE(8);
      const total_bins = msg.readUInt16LE(10) || 1024;

      if (partialSpectrum.block_id !== block_id) {
        partialSpectrum = { block_id, bins: new Array(total_bins).fill(0), receivedParts: new Set() };
      }

      const offset = part * 512;
      for (let i = 0; i < 512 && (offset + i) < partialSpectrum.bins.length; i++) {
        partialSpectrum.bins[offset + i] = msg.readUInt16LE(12 + i * 2);
      }
      partialSpectrum.receivedParts.add(part);

      // Si se recibieron todas las partes necesarias (ej. 2 para 1024 bins, 4 para 2048 bins)
      const requiredParts = Math.ceil(total_bins / 512);
      if (partialSpectrum.receivedParts.size >= requiredParts) {
        const payload = {
          type: 'spectrum',
          block_id,
          total_bins: partialSpectrum.bins.length,
          bins: partialSpectrum.bins
        };
        lastSpectrum = payload;
        broadcastSSE('spectrum', payload);
      }
    }
  } else if (msg.length === 1032) {
    // Compatibilidad retroactiva con paquetes de 512 bins
    const magic = msg.readUInt32LE(0);
    if (magic === UDP_MAGIC_SPECTRUM) {
      const block_id = msg.readUInt32LE(4);
      const bins = [];
      for (let i = 0; i < 512; i++) {
        bins.push(msg.readUInt16LE(8 + i * 2));
      }

      const payload = {
        type: 'spectrum',
        block_id,
        total_bins: 512,
        bins
      };

      lastSpectrum = payload;
      broadcastSSE('spectrum', payload);
    }
  }
});

udpSocket.bind(0, () => {
  console.log(`[UDP] Socket enlazado en puerto ${udpSocket.address().port}`);
});

function sendBoardCommand(cmd) {
  const buf = Buffer.from(cmd + '\n', 'ascii');
  udpSocket.send(buf, 0, buf.length, BOARD_PORT, BOARD_IP, (err) => {
    if (err) console.error(`[UDP] Error enviando '${cmd}': ${err.message}`);
  });
}

// Heartbeat UDP hacia la tarjeta mientras haya navegadores abiertos
setInterval(() => {
  if (sseClients.size > 0) {
    sendBoardCommand('STREAM');
  }
}, 3000);

// Solicitar espectro completo 4 veces por segundo (cada 250ms) si hay clientes
setInterval(() => {
  if (sseClients.size > 0) {
    sendBoardCommand('SPECTRUM');
  }
}, 250);

function broadcastSSE(event, data) {
  if (sseClients.size === 0) return;
  const str = `event: ${event}\ndata: ${JSON.stringify(data)}\n\n`;
  for (const client of sseClients) {
    client.write(str);
  }
}

// Servidor HTTP
const server = http.createServer((req, res) => {
  // CORS para cualquier origen local
  res.setHeader('Access-Control-Allow-Origin', '*');

  if (req.url === '/api/events') {
    res.writeHead(200, {
      'Content-Type': 'text/event-stream',
      'Cache-Control': 'no-cache, no-transform',
      'Connection': 'keep-alive'
    });

    sseClients.add(res);
    console.log(`[HTTP] Cliente conectado a SSE (Total activos: ${sseClients.size})`);

    // Iniciar streaming en la tarjeta de inmediato
    sendBoardCommand('STREAM');
    sendBoardCommand('SPECTRUM');

    // Enviar ultimo estado si existe
    if (lastTelemetry) {
      res.write(`event: telemetry\ndata: ${JSON.stringify(lastTelemetry)}\n\n`);
    }
    if (lastSpectrum) {
      res.write(`event: spectrum\ndata: ${JSON.stringify(lastSpectrum)}\n\n`);
    }

    req.on('close', () => {
      sseClients.delete(res);
      console.log(`[HTTP] Cliente desconectado de SSE (Total activos: ${sseClients.size})`);
      if (sseClients.size === 0) {
        sendBoardCommand('STOP');
      }
    });
    return;
  }

  if (req.url === '/api/telemetry') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(lastTelemetry || { status: 'waiting_data' }));
    return;
  }

  if (req.url === '/api/spectrum') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(lastSpectrum || { status: 'waiting_data' }));
    return;
  }

  if (req.url === '/api/status') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      board_ip: BOARD_IP,
      board_port: BOARD_PORT,
      clients_count: sseClients.size,
      last_telemetry: lastTelemetry
    }));
    return;
  }

  // Archivo estatico index.html
  const indexPath = path.join(__dirname, 'public', 'index.html');
  fs.readFile(indexPath, (err, data) => {
    if (err) {
      res.writeHead(500, { 'Content-Type': 'text/plain' });
      res.end('Error al leer index.html: ' + err.message);
      return;
    }
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    res.end(data);
  });
});

server.listen(HTTP_PORT, '0.0.0.0', () => {
  // Detectar IPs locales
  const interfaces = os.networkInterfaces();
  const addresses = [];
  for (const name of Object.keys(interfaces)) {
    for (const net of interfaces[name]) {
      if (net.family === 'IPv4' && !net.internal) {
        addresses.push({ iface: name, ip: net.address });
      }
    }
  }

  console.log('================================================================');
  console.log('  STM32H755 DUAL-CORE FFT + TELEMETRIA WEB MONITOR (LOCALHOST)');
  console.log('================================================================');
  console.log(` > Servidor HTTP escuchando en puerto ${HTTP_PORT}`);
  console.log(` > En tu PC:       http://localhost:${HTTP_PORT}`);
  addresses.forEach(a => {
    console.log(` > En tu CELULAR:  http://${a.ip}:${HTTP_PORT}  (${a.iface})`);
  });
  console.log(` > Tarjeta Nucleo: ${BOARD_IP}:${BOARD_PORT}`);
  console.log('================================================================');
  console.log('Conecta tu celular a la misma red Wi-Fi y abre la URL indicada.');
});
