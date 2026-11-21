import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { test } from 'node:test';

test('Wasm models sustain, stay finite under polyphonic load, and release at native rates', async () => {
  const wasm = await readFile(new URL('../../docs/assets/ocarina.wasm', import.meta.url));
  const { instance } = await WebAssembly.instantiate(wasm);
  const engine = instance.exports;
  engine._initialize?.();
  const memory = new Float32Array(engine.memory.buffer);
  const render = () => {
    const offset = engine.render(256) / Float32Array.BYTES_PER_ELEMENT;
    return memory.subarray(offset, offset + 256);
  };

  for (const sampleRate of [44100, 48000, 88200, 96000]) {
    for (let model = 0; model < 4; ++model) {
      engine.performance(model, 16);
      engine.prepare(sampleRate);
      assert.ok(render().every(sample => sample === 0));

      engine.note(1, 69, 1);
      const warmup = Math.ceil(sampleRate * 0.3 / 256);
      for (let block = 0; block < warmup; ++block) render();
      let crossings = 0;
      let armed = false;
      let previous = 0;
      for (let block = 0; block < warmup; ++block) {
        for (const sample of render()) {
          armed ||= sample < -0.025;
          if (armed && previous <= 0 && sample > 0) {
            armed = false;
            ++crossings;
          }
          previous = sample;
        }
      }
      assert.ok(Math.abs(crossings * sampleRate / (warmup * 256) - 440) < 9);

      for (let note = 48; note < 64; ++note) engine.note(1, note, 2);
      let peak = 0;
      for (let block = 0; block < 50; ++block) {
        for (const sample of render()) peak = Math.max(peak, Math.abs(sample));
        assert.ok(Number.isFinite(peak) && peak < 1);
      }
      assert.ok(peak > 0.1);
      engine.release();
      for (let block = 0; block < warmup; ++block) render();
      assert.ok(render().every(sample => sample === 0));
    }
  }
});
