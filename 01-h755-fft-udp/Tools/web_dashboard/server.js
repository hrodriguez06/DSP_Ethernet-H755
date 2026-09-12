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
  if (msg.length === 70 || msg.length === 60) {
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
        harmonics
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

      // Si se recibieron ambas partes (o si solo consta de 1)
      if (partialSpectrum.receivedParts.has(0) && (total_bins <= 512 || partialSpectrum.receivedParts.has(1))) {
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
