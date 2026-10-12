// Generates the looping background music for UNREAL-game (Tetris).
// Original chiptune in C minor -- written from scratch, no external samples.
// Run: node Tools/GenerateMusic.mjs   -> Content/Sounds/M_Music.wav
import { writeFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const SR = 44100;
const BPM = 152;
const beat = 60 / BPM;
const bar = beat * 4;
const BARS = 8;
const N = Math.round(bar * BARS * SR);
const buf = new Float32Array(N);

const mtof = (m) => 440 * Math.pow(2, (m - 69) / 12);

function osc(shape, p) {
  const ph = p - Math.floor(p);
  if (shape === "square") return ph < 0.5 ? 1 : -1;
  if (shape === "pulse") return ph < 0.25 ? 1 : -1;
  if (shape === "saw") return 2 * ph - 1;
  if (shape === "tri") return 4 * Math.abs(ph - 0.5) - 1;
  return Math.sin(2 * Math.PI * ph);
}

// Write one sustained note into the mix.
function tone(start, dur, freq, shape, gain, decay, detune = 0) {
  const s0 = Math.round(start * SR);
  const n = Math.round(dur * SR);
  let p1 = 0, p2 = 0;
  for (let i = 0; i < n; i++) {
    const idx = s0 + i;
    if (idx >= N) break;
    const env = Math.pow(1 - i / n, decay);
    p1 += freq / SR;
    let v = osc(shape, p1);
    if (detune) {
      p2 += (freq * (1 + detune)) / SR;
      v = (v + osc(shape, p2)) * 0.5;
    }
    buf[idx] += v * env * gain;
  }
}

function kick(start) {
  const s0 = Math.round(start * SR);
  const n = Math.round(0.18 * SR);
  let ph = 0;
  for (let i = 0; i < n; i++) {
    const idx = s0 + i;
    if (idx >= N) break;
    const f = 140 * Math.pow(1 - i / n, 3) + 45;
    ph += f / SR;
    buf[idx] += Math.sin(2 * Math.PI * ph) * Math.pow(1 - i / n, 4) * 0.55;
  }
}

let seed = 1337;
function hat(start, gain) {
  const s0 = Math.round(start * SR);
  const n = Math.round(0.05 * SR);
  for (let i = 0; i < n; i++) {
    const idx = s0 + i;
    if (idx >= N) break;
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    const noise = (seed / 0x3fffffff) - 1;
    buf[idx] += noise * Math.pow(1 - i / n, 6) * gain;
  }
}

// --- harmony -----------------------------------------------------------------
// root/mid/third are MIDI numbers; bass is the low octave root.
const CH = {
  Cm: { root: 60, mid: 63, top: 67, bass: 36 },
  Ab: { root: 56, mid: 60, top: 63, bass: 44 },
  Eb: { root: 63, mid: 67, top: 70, bass: 39 },
  Bb: { root: 58, mid: 62, top: 65, bass: 46 },
  G:  { root: 55, mid: 59, top: 62, bass: 43 },
};
const PROG = ["Cm", "Cm", "Ab", "Eb", "Bb", "Cm", "Ab", "G"];

// Lead: 8 eighth-note slots per bar, null = rest. MIDI numbers.
const LEAD = [
  [72, null, 75, null, 79, 77, 75, null],
  [67, null, 72, null, 70, null, 67, null],
  [68, null, 72, null, 75, 72, 70, null],
  [67, null, 70, null, 75, null, 79, null],
  [77, null, 74, null, 70, null, 74, null],
  [75, null, 79, null, 72, null, 75, null],
  [72, null, 68, null, 63, null, 68, null],
  [71, null, 74, null, 79, null, 77, 75],
];

for (let b = 0; b < BARS; b++) {
  const t0 = b * bar;
  const c = CH[PROG[b]];

  // bass: steady eighths, with an octave lift on the offbeats
  for (let i = 0; i < 8; i++) {
    const n = i % 4 === 2 ? c.bass + 12 : c.bass;
    tone(t0 + i * beat * 0.5, beat * 0.46, mtof(n), "saw", 0.20, 1.4);
  }

  // arpeggio: 16ths cycling root-third-top-third, one octave above the chord
  const arp = [c.root, c.mid, c.top, c.mid];
  for (let i = 0; i < 16; i++) {
    tone(t0 + i * beat * 0.25, beat * 0.23, mtof(arp[i % 4] + 12), "pulse", 0.085, 2.2);
  }

  // lead
  LEAD[b].forEach((m, i) => {
    if (m == null) return;
    tone(t0 + i * beat * 0.5, beat * 0.46, mtof(m), "square", 0.17, 0.9, 0.004);
  });

  // drums
  kick(t0);
  kick(t0 + beat * 2);
  for (let i = 0; i < 8; i++) hat(t0 + i * beat * 0.5, i % 2 ? 0.05 : 0.09);
}

// --- normalise + write 16-bit PCM WAV ----------------------------------------
let peak = 0;
for (let i = 0; i < N; i++) peak = Math.max(peak, Math.abs(buf[i]));
const norm = peak > 0 ? 0.89 / peak : 1;

const data = Buffer.alloc(N * 2);
for (let i = 0; i < N; i++) {
  data.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(buf[i] * norm * 32767))), i * 2);
}

const header = Buffer.alloc(44);
header.write("RIFF", 0);
header.writeUInt32LE(36 + data.length, 4);
header.write("WAVE", 8);
header.write("fmt ", 12);
header.writeUInt32LE(16, 16);
header.writeUInt16LE(1, 20);          // PCM
header.writeUInt16LE(1, 22);          // mono
header.writeUInt32LE(SR, 24);
header.writeUInt32LE(SR * 2, 28);     // byte rate
header.writeUInt16LE(2, 32);          // block align
header.writeUInt16LE(16, 34);         // bits per sample
header.write("data", 36);
header.writeUInt32LE(data.length, 40);

const here = dirname(fileURLToPath(import.meta.url));
const out = join(here, "..", "Content", "Sounds", "M_Music.wav");
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, Buffer.concat([header, data]));

console.log(`wrote ${out}`);
console.log(`  ${(N / SR).toFixed(2)}s @ ${SR}Hz mono 16-bit, peak ${peak.toFixed(3)}`);
