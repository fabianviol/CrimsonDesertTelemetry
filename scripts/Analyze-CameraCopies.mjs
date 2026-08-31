// Offline diagnostic only. Does not attach to the game or modify input files.
import { createReadStream } from 'node:fs';
import { createInterface } from 'node:readline';

if (process.argv.length !== 3) throw new Error('Usage: node Analyze-CameraCopies.mjs <trace.jsonl>');
const stats = new Map();
const yaw = c => Math.atan2(c.forward.x, c.forward.z) * 180 / Math.PI;
const wrap = angle => ((angle + 540) % 360) - 180;
const round = x => Number(x.toFixed(3));
const fresh = address => ({ address, samples: 0, changes: 0, positive: 0, negative: 0,
  maxStep: 0, largeSteps: 0, firstChangeMs: null, lastChangeMs: null,
  totalTravel: 0, netTravel: 0, selected: 0, gaps: 0 });
const selected = fresh('consensus');
let metadata, samples = 0, elapsedMs = 0;
const windows = new Map();
function update(s, c, frame) {
  const value = yaw(c);
  s.samples++;
  if (s.lastSequence != null && frame.sequence !== s.lastSequence + 1) s.gaps++;
  if (s.lastYaw != null) {
    const delta = wrap(value - s.lastYaw);
    const magnitude = Math.abs(delta);
    if (magnitude > 0.05) {
      s.changes++;
      if (delta > 0) s.positive++; else s.negative++;
      if (magnitude > 15) s.largeSteps++;
      s.maxStep = Math.max(s.maxStep, magnitude);
      s.totalTravel += magnitude;
      s.netTravel += delta;
      s.firstChangeMs ??= frame.elapsedMs;
      s.lastChangeMs = frame.elapsedMs;
    }
  }
  s.firstYaw ??= value;
  s.lastYaw = value;
  s.lastSequence = frame.sequence;
}
for await (const line of createInterface({ input: createReadStream(process.argv[2]), crlfDelay: Infinity })) {
  if (!line) continue;
  const frame = JSON.parse(line);
  if (frame.kind === 'camera-copy-trace') { metadata = frame; continue; }
  if (frame.kind !== 'sample') throw new Error('Unexpected trace record');
  samples++;
  elapsedMs = frame.elapsedMs;
  const bucket = Math.floor(elapsedMs / 5000) * 5;
  let window = windows.get(bucket);
  if (!window) windows.set(bucket, window = { seconds: bucket, frames: 0, validSum: 0,
    consensusSum: 0, directions: new Set(), firstYaw: null, lastYaw: null });
  window.frames++;
  window.validSum += frame.candidates.length;
  if (frame.consensus) {
    const camera = frame.consensus.camera;
    update(selected, camera, frame);
    window.consensusSum += frame.consensus.copyCount;
    window.directions.add(JSON.stringify(camera.forward));
    window.firstYaw ??= yaw(camera);
    window.lastYaw = yaw(camera);
  }
  for (const c of frame.candidates) {
    let s = stats.get(c.address);
    if (!s) stats.set(c.address, s = fresh(`0x${c.address.toString(16).toUpperCase()}`));
    update(s, c, frame);
    if (frame.consensus?.camera.address === c.address) s.selected++;
  }
}
const clean = s => Object.fromEntries(Object.entries(s).filter(([k]) => k !== 'lastSequence')
  .map(([k,v]) => [k, typeof v === 'number' ? round(v) : v]));
console.log(JSON.stringify({ samples, elapsedMs: round(elapsedMs), discovered: metadata?.addresses.length,
  validAddresses: stats.size, consensus: clean(selected),
  windows: [...windows.values()].map(w => ({ seconds: w.seconds, frames: w.frames,
    meanValid: round(w.validSum / w.frames), meanConsensus: round(w.consensusSum / w.frames),
    distinctSelectedDirections: w.directions.size, firstYaw: w.firstYaw, lastYaw: w.lastYaw })),
  copies: [...stats.values()].sort((a,b) => b.changes - a.changes).slice(0,12).map(clean)
}, null, 2));
