// Node.js 22+; no dependencies. This client does not start the telemetry host.
import { pathToFileURL } from 'node:url';

// This is a consumer guard, not a complete JSON Schema validator.
export function usableSnapshot(s, nowMs = Date.now(), maxAgeMs = 1500) {
  return String(s?.schemaVersion).split('.')[0] === '1'
    && Number.isSafeInteger(s?.sequence) && s.sequence >= 0
    && s?.game?.state === 'playing' && !!s.player?.position && !!s.camera?.forward
    && Array.isArray(s.capabilities)
    && s.capabilities.includes('player.position') && s.capabilities.includes('camera.transform')
    && Number.isFinite(Date.parse(s.capturedAt))
    && Math.abs(nowMs - Date.parse(s.capturedAt)) <= maxAgeMs;
}

export function run(url = 'ws://127.0.0.1:27311/v1/stream', maxAgeMs = 1500) {
  let socket, reconnectTimer, stopped = false;
  let latest = null, lastSequence = -1, lastFreshAt = 0;

  function onSnapshot(snapshot) {
    // Replace this display callback with your integration. null means clear output.
    const orientation = snapshot?.capabilities.includes('player.orientation')
      ? snapshot.player.orientation : null;
    console.log(snapshot ? JSON.stringify({
      sequence: snapshot.sequence,
      playerHeadingDegrees: orientation?.headingDegrees ?? null,
      cameraForward: snapshot.camera.forward,
    }) : 'unavailable');
  }

  function clear() {
    if (latest !== null) { latest = null; onSnapshot(null); }
  }

  function connect() {
    if (stopped) return;
    lastSequence = -1; // A restarted host may begin at zero again.
    socket = new WebSocket(url);
    socket.addEventListener('message', event => {
      try {
        const sample = JSON.parse(event.data); // The API gives complete text messages.
        if (!Number.isSafeInteger(sample?.sequence) || sample.sequence < 0) { clear(); return; }
        if (sample.sequence <= lastSequence) return; // Do not refresh the watchdog.
        lastSequence = sample.sequence;
        if (!usableSnapshot(sample, Date.now(), maxAgeMs)) { clear(); return; }
        latest = sample;
        lastFreshAt = performance.now();
        onSnapshot(latest);
      } catch { clear(); }
    });
    socket.addEventListener('error', () => { clear(); socket.close(); });
    socket.addEventListener('close', () => {
      clear();
      if (!stopped) reconnectTimer = setTimeout(connect, 1000);
    });
  }

  onSnapshot(null);
  const watchdog = setInterval(() => {
    if (latest !== null && performance.now() - lastFreshAt > maxAgeMs) clear();
  }, 250);
  connect();
  return () => {
    stopped = true;
    clearTimeout(reconnectTimer);
    clearInterval(watchdog);
    clear();
    socket?.close();
  };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const stop = run(process.argv[2]);
  process.once('SIGINT', stop);
  process.once('SIGTERM', stop);
}
