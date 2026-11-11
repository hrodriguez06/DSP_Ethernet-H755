/**
 * Cliente SSE: Conexión a /api/events para recibir telemetría y espectro en tiempo real.
 */
export class SSEClient {
  constructor(endpoint = '/api/events') {
    this.endpoint = endpoint;
    this.evtSource = null;
    this.onOpen = null;
    this.onError = null;
    this.onTelemetry = null;
    this.onSpectrum = null;
  }

  connect() {
    this.evtSource = new EventSource(this.endpoint);

    this.evtSource.onopen = () => {
      if (this.onOpen) this.onOpen();
    };

    this.evtSource.onerror = (err) => {
      if (this.onError) this.onError(err);
    };

    this.evtSource.addEventListener('telemetry', (e) => {
      try {
        const data = JSON.parse(e.data);
        if (this.onTelemetry) this.onTelemetry(data);
      } catch (err) {
        console.error('[SSE] Error procesando telemetría:', err);
      }
    });

    this.evtSource.addEventListener('spectrum', (e) => {
      try {
        const data = JSON.parse(e.data);
        if (this.onSpectrum) this.onSpectrum(data);
      } catch (err) {
        console.error('[SSE] Error procesando espectro:', err);
      }
    });
  }

  disconnect() {
    if (this.evtSource) {
      this.evtSource.close();
      this.evtSource = null;
    }
  }
}
