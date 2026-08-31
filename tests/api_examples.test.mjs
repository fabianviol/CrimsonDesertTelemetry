// Optional documentation/example checks: Node.js 22+ and Python 3.10+, no packages.
// Override the Python executable with CDT_PYTHON if it is not on PATH.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { fileURLToPath } from 'node:url';
import { setTimeout as delay } from 'node:timers/promises';
import { run, usableSnapshot } from '../examples/websocket_client.mjs';

const execute = promisify(execFile);
const document = await readFile(new URL('../docs/API.md', import.meta.url), 'utf8');
const examples = [...document.matchAll(/```json\s*\n([\s\S]*?)\n```/g)].map(m => JSON.parse(m[1]));
const playing = examples.find(s => s.game?.state === 'playing');
const loading = examples.find(s => s.game?.state === 'loading');
const health = examples.find(s => s.status === 'playing');
const fresh = () => ({ ...structuredClone(playing), capturedAt: new Date().toISOString() });

test('documented snapshots have the schema-required envelope and vectors', async () => {
  const schema = JSON.parse(await readFile(new URL('../schema/telemetry-v1.schema.json', import.meta.url)));
  for (const snapshot of [playing, loading]) {
    assert.deepEqual(Object.keys(snapshot).sort(), [...schema.required].sort());
    assert.equal(snapshot.schemaVersion, schema.properties.schemaVersion.const);
    assert.ok(schema.properties.game.properties.state.enum.includes(snapshot.game.state));
    assert.ok(Number.isFinite(Date.parse(snapshot.capturedAt)));
  }
  assert.deepEqual(Object.keys(playing.camera).sort(),
    [...schema.properties.camera.oneOf[1].required].sort());
  assert.equal(playing.camera.farPlane, null);
  for (const vector of [playing.player.position, playing.player.orientation.forward,
    playing.player.orientation.up, playing.camera.position, playing.camera.forward,
    playing.camera.right, playing.camera.up]) {
    assert.deepEqual(Object.keys(vector).sort(), ['x', 'y', 'z']);
    assert.ok(Object.values(vector).every(Number.isFinite));
  }
  assert.equal(loading.player, null);
  assert.equal(loading.camera, null);
  assert.equal(loading.quality, null);
});

test('WebSocket guard rejects unavailable, stale, malformed and incompatible samples', () => {
  const sample = fresh();
  assert.equal(usableSnapshot(sample), true);
  for (const invalid of [null, {}, loading,
    { ...sample, schemaVersion: '2.0' }, { ...sample, sequence: -1 },
    { ...sample, capturedAt: 'invalid' }, { ...sample, capturedAt: '2000-01-01T00:00:00Z' },
    { ...sample, camera: null }, { ...sample, capabilities: [] }]) {
    assert.equal(usableSnapshot(invalid), false);
  }
  sample.player.orientation = null;
  sample.capabilities = sample.capabilities.filter(c => c !== 'player.orientation');
  assert.equal(usableSnapshot(sample), true); // Orientation is optional.
});

test('WebSocket example clears stale state, skips duplicates and accepts a restarted sequence', async () => {
  const originalSocket = globalThis.WebSocket;
  const originalLog = console.log;
  const sockets = [], displayed = [];
  class FakeSocket extends EventTarget {
    constructor() { super(); sockets.push(this); }
    emit(snapshot) { this.dispatchEvent(new MessageEvent('message', { data: JSON.stringify(snapshot) })); }
    close() { this.dispatchEvent(new Event('close')); }
  }
  let stop;
  try {
    globalThis.WebSocket = FakeSocket;
    console.log = value => displayed.push(value);
    stop = run('ws://127.0.0.1:27311/v1/stream', 25);
    const sample = fresh();
    sockets[0].emit(sample);
    assert.equal(displayed.length, 2); // Initial unavailable, then one sample.
    sockets[0].emit(sample);
    sockets[0].emit({ ...sample, sequence: sample.sequence - 1 });
    assert.equal(displayed.length, 2);
    await delay(350); // The 250 ms watchdog clears the unrefreshed sample.
    assert.equal(displayed.at(-1), 'unavailable');
    sockets[0].close();
    await delay(1100);
    assert.equal(sockets.length, 2);
    sockets[1].emit({ ...fresh(), sequence: 0 });
    assert.equal(JSON.parse(displayed.at(-1)).sequence, 0);
    sockets[1].emit({ ...loading, sequence: 1 });
    assert.equal(displayed.at(-1), 'unavailable');
  } finally {
    stop?.();
    globalThis.WebSocket = originalSocket;
    console.log = originalLog;
  }
});

test('Python example handles live data, optional orientation, 503 and stale snapshots', async () => {
  let mode = 'playing';
  const server = createServer((request, response) => {
    response.setHeader('Content-Type', 'application/json');
    if (request.url === '/v1/health') { response.end(JSON.stringify(health)); return; }
    if (mode === '503') {
      response.statusCode = 503;
      response.end(JSON.stringify({ ...health, status: 'waiting-for-game' }));
      return;
    }
    const sample = fresh();
    if (mode === 'optional') {
      sample.player.orientation = null;
      sample.capabilities = sample.capabilities.filter(c => c !== 'player.orientation');
    }
    if (mode === 'stale') sample.capturedAt = '2000-01-01T00:00:00Z';
    response.end(JSON.stringify(sample));
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const args = [fileURLToPath(new URL('../examples/http_snapshot.py', import.meta.url)),
    `http://127.0.0.1:${server.address().port}`];
  try {
    const call = () => execute(process.env.CDT_PYTHON || 'python', args, { timeout: 5000 });
    assert.equal(JSON.parse((await call()).stdout).playerHeadingDegrees, 0);
    mode = 'optional';
    assert.equal(JSON.parse((await call()).stdout).playerHeadingDegrees, null);
    for (mode of ['503', 'stale']) {
      await assert.rejects(call(), error => error.code === 1 && error.stdout === '');
    }
  } finally {
    await new Promise(resolve => server.close(resolve));
  }
});
