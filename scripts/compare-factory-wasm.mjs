import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { PresetFormat } from '../docs/assets/presets.js';

const [windowsPath, androidPath] = process.argv.slice(2);
assert.ok(windowsPath && androidPath, 'Usage: node scripts/compare-factory-wasm.mjs <windows.tsv> <android.tsv>');

const assets = new URL('../docs/assets/', import.meta.url);
const catalog = JSON.parse(await readFile(new URL('parameters.json', assets), 'utf8'));
const format = new PresetFormat(catalog.controls, catalog.effects);
const { instance } = await WebAssembly.instantiate(await readFile(new URL('ocarina.wasm', assets)));
const engine = instance.exports;
engine._initialize?.();

const rate = 48_000;
const blockSize = 256;
const frames = new Float32Array(engine.memory.buffer);
const wasmReport = [];
for (const preset of catalog.factory) {
  format.values(preset).forEach((value, index) => engine.parameter(index, value));
  engine.prepare(rate);
  engine.note(1, 60, 1);
  let power = 0;
  let peak = 0;
  let stereo = 0;
  const points = [0, rate / 2, rate, rate * 2];
  for (let part = 0; part < points.length - 1; ++part) {
    let frame = points[part];
    if (frame === rate) engine.note(0, 60, 1);
    while (frame < points[part + 1]) {
      const count = Math.min(blockSize, points[part + 1] - frame);
      const offset = engine.render(count) / Float32Array.BYTES_PER_ELEMENT;
      for (let index = 0; index < count; ++index) {
        const left = frames[offset + index];
        const right = frames[offset + blockSize + index];
        assert.ok(Number.isFinite(left) && Number.isFinite(right), preset.id + ' has non-finite output');
        if (part === 1) power += left ** 2 + right ** 2;
        peak = Math.max(peak, Math.abs(left), Math.abs(right));
        stereo += Math.abs(left - right);
      }
      frame += count;
    }
  }
  wasmReport.push([preset.id, 10 * Math.log10(power / rate), peak, stereo]);
}

async function readReport(path) {
  return (await readFile(path, 'utf8')).trim().split(/\r?\n/).map(line => line.split('\t'));
}

const reports = { Windows: await readReport(windowsPath), Wasm: wasmReport, Android: await readReport(androidPath) };
const ids = catalog.factory.map(preset => preset.id);
assert.equal(ids.length, 40);
for (const [name, rows] of Object.entries(reports)) {
  assert.deepEqual(rows.map(row => row[0]), ids);
  const levels = rows.map(row => Number(row[1]));
  const peak = Math.max(...rows.map(row => Number(row[2])));
  assert.ok(levels.every(level => Math.abs(level + 21) <= 0.85) && peak < 0.96, name + ' is outside preset calibration');
  assert.ok(rows.every(row => Number.isFinite(Number(row[3])) && Number(row[3]) > 0), name + ' has missing stereo output');
  console.error(name + ' RMS range ' + Math.min(...levels).toFixed(5) + ' to ' +
    Math.max(...levels).toFixed(5) + ' dBFS, peak ' + peak.toFixed(5));
}

function maximumDifference(column) {
  return Math.max(...ids.map((_, index) => {
    const values = Object.values(reports).map(rows => Number(rows[index][column]));
    return Math.max(...values) - Math.min(...values);
  }));
}

const rmsDifference = maximumDifference(1);
const peakDifference = maximumDifference(2);
console.error('Maximum per-preset difference ' + rmsDifference.toFixed(6) +
  ' dB RMS and ' + peakDifference.toFixed(6) + ' linear peak');
assert.ok(rmsDifference < 0.05 && peakDifference < 0.02, 'Renderers exceeded cross-platform tolerances');
console.log(wasmReport.map(row => row.join('\t')).join('\n'));
