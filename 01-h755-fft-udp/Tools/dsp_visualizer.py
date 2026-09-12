#!/usr/bin/env python3
"""
dsp_visualizer.py
Cliente de telemetria UDP para 3_DSP_Ethernet / 01-h755-fft-udp.

Recibe los datagramas binarios en el puerto 5002 emitidos por el Cortex-M7
y muestra en tiempo real:
  - Deteccion de senal periodica (estado, f0, amplitud, SNR, THD).
  - Componentes armonicas (f0, 2f0, 3f0, 4f0, 5f0).
  - Grafico del espectro completo (512 bins) si matplotlib esta disponible.
"""

import sys
import time
import socket
import struct
import argparse

TELEMETRY_MAGIC = 0x44535032  # "DSP2"
SPECTRUM_MAGIC  = 0x53504543  # "SPEC"
TELEMETRY_FMT   = "<11I5H3H"  # 11 uint32, 5 uint16, 3 uint16 rsv = 60 bytes

def run_console_client(board_ip, port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(2.0)

    print(f"Conectando con tarjeta STM32H755 en {board_ip}:{port}...")
    try:
        # Enviar comando STREAM para suscribirse
        sock.sendto(b"STREAM\n", (board_ip, port))
        resp, _ = sock.recvfrom(256)
        print(f"Respuesta de la placa: {resp.decode(errors='ignore').strip()}")
    except socket.timeout:
        print("Aviso: No hubo respuesta inmediata al comando STREAM (esperando datagramas...)")

    print("\r\nEscuchando telemetria espectral a 25 Hz (Ctrl+C para salir)...\n")
    print("-" * 92)
    print(f"{'Bloque':>8} | {'Estado':^10} | {'f0 (Hz)':>8} | {'Vpk(mV)':>7} | {'Vdc(mV)':>7} | {'Vpp(mV)':>7} | {'THD (%)':>7} | {'SNR (dB)':>7}")
    print("-" * 92)

    sock.settimeout(3.0)
    try:
        while True:
            try:
                data, addr = sock.recvfrom(2048)
                if len(data) == struct.calcsize(TELEMETRY_FMT):
                    fields = struct.unpack(TELEMETRY_FMT, data)
                    magic = fields[0]
                    if magic == TELEMETRY_MAGIC:
                        block_id = fields[1]
                        t_ms     = fields[2]
                        fs       = fields[3]
                        f0_hz    = fields[5] / 10.0
                        f0_mag   = fields[6]
                        noise    = fields[7]
                        snr      = fields[8] / 10.0
                        thd      = fields[9] / 10.0
                        flags    = fields[10]
                        harmonics = fields[11:16]
                        dc_offset = fields[16]
                        v_pp      = fields[17]

                        detected = (flags & (1 << 1)) != 0
                        status_str = "PERIODICA" if detected else "  RUIDO  "

                        print(f"{block_id:>8} | {status_str:^10} | {f0_hz:>8.1f} | {f0_mag:>7} | {dc_offset:>7} | {v_pp:>7} | {thd:>7.1f} | {snr:>7.1f}")
                elif len(data) == 1032:
                    # Paquete de espectro completo
                    spec_magic, block_id = struct.unpack("<2I", data[:8])
                    if spec_magic == SPECTRUM_MAGIC:
                        bins = struct.unpack("<512H", data[8:])
                        print(f"[ESPECTRO] Bloque {block_id}: 512 bins recibidos (max bin: {max(bins)} mV)")
            except socket.timeout:
                # Reenviar comando STREAM por si expiro
                sock.sendto(b"STREAM\n", (board_ip, port))
    except KeyboardInterrupt:
        print("\r\nDeteniendo streaming...")
        try:
            sock.sendto(b"STOP\n", (board_ip, port))
        except Exception:
            pass
        print("Finalizado.")

def main():
    parser = argparse.ArgumentParser(description="Cliente UDP para 3_DSP_Ethernet")
    parser.add_argument("--ip", default="192.168.1.172", help="Direccion IP de la tarjeta Nucleo (def: 192.168.1.172)")
    parser.add_argument("--port", type=int, default=5002, help="Puerto UDP del servidor DSP (5002)")
    args = parser.parse_args()

    run_console_client(args.ip, args.port)

if __name__ == "__main__":
    main()
