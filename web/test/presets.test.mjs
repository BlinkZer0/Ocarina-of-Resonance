import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { test } from 'node:test';
import { chromium } from 'playwright-core';
import { startServer } from '../server.mjs';
import { PresetFormat } from '../../docs/assets/presets.js';

const root = new URL('../../', import.meta.url);
const manifest = JSON.parse(await readFile(new URL('docs/assets/parameters.json', root), 'utf8'));
const format = new PresetFormat(manifest.controls, manifest.effects);

test('renders all forty factory snapshots and preserves the v1 preset contract', async () => {
  assert.equal(manifest.factory.length, 40);
  assert.equal(new Set(manifest.factory.map(preset => preset.id)).size, 40);
  const legacy = JSON.parse(await readFile(new URL('tests/fixtures/reference.orpreset', root), 'utf8'));
  const migrated = format.read(legacy);
  assert.equal(migrated.performance.tempoBpm, 120);
  assert.ok(migrated.effects.every(effect => !effect.enabled));

  const wasm = await readFile(new URL('docs/assets/ocarina.wasm', root));
  const { instance } = await WebAssembly.instantiate(wasm);
  const engine = instance.exports;
  engine._initialize?.();
  const memory = new Float32Array(engine.memory.buffer);
  for (const preset of manifest.factory) {
    assert.deepEqual(format.capture(format.values(preset), preset), preset, preset.name);
    format.values(preset).forEach((value, index) => engine.parameter(index, value));
    engine.prepare(48000);
    engine.note(1, 60, 1);
    let power = 0, peak = 0, stereo = 0;
    for (const [start, end] of [[0, 24000], [24000, 48000], [48000, 96000]]) {
      if (start === 48000) engine.note(0, 60, 1);
      for (let frame = start; frame < end;) {
        const count = Math.min(256, end - frame);
        const offset = engine.render(count) / 4;
        for (let i = 0; i < count; ++i) {
          const left = memory[offset + i], right = memory[offset + 256 + i];
          if (start === 24000) power += left * left + right * right;
          peak = Math.max(peak, Math.abs(left), Math.abs(right));
          stereo += Math.abs(left - right);
        }
        frame += count;
      }
    }
    const level = 10 * Math.log10(power / 48000);
    assert.ok(Number.isFinite(peak) && peak < .96 && stereo > 0, preset.name);
    assert.ok(Math.abs(level + 21) <= .85, `${preset.name}: ${level} dBFS RMS`);
  }
  assert.equal(manifest.factory.find(p => p.name === 'FM Airglass').effects.find(fx => fx.id === 'fm').enabled, true);
  assert.equal(manifest.factory.find(p => p.name === 'Granular Illusion').effects.find(fx => fx.id === 'grain').enabled, true);
});

test('loads a factory bank, edits effects, and saves, renames, exports and imports a portable patch', async () => {
  const { server, address } = await startServer(0);
  const browser = await chromium.launch({ executablePath: process.env.OCARINA_BROWSER || undefined });
  try {
    const page = await browser.newPage({ acceptDownloads: true });
    await page.goto(address);
    const presets = page.getByLabel('Preset', { exact: true });
    await presets.locator('option', { hasText: 'Granular Illusion' }).waitFor({ state: 'attached' });
    assert.equal(await presets.locator('option').count(), 41);
    await page.getByLabel('Bank').selectOption('Electronic');
    assert.equal(await presets.locator('option').count(), 11);
    await presets.selectOption('granular-illusion');
    await page.locator('#effects > summary').click();
    await page.locator('.effect > summary').filter({ hasText: /^Granular$/ }).click();
    const granular = page.getByRole('checkbox', { name: 'Granular enabled' });
    assert.ok(await granular.isChecked());
    await granular.uncheck();
    await page.getByRole('button', { name: 'Save', exact: true }).click();
    const dialog = page.getByRole('dialog');
    await dialog.getByLabel('Preset name').fill('Transfer check');
    await dialog.getByRole('button', { name: 'Save' }).click();
    await page.getByText('Saved Transfer check.').waitFor();
    await page.getByRole('button', { name: 'Rename', exact: true }).click();
    await dialog.getByLabel('Preset name').fill('Portable wind');
    await dialog.getByRole('button', { name: 'Rename' }).click();
    await page.getByText('Renamed to Portable wind.').waitFor();

    const download = page.waitForEvent('download');
    await page.getByRole('button', { name: 'Export', exact: true }).click();
    const archive = await download;
    const path = await archive.path();
    assert.match(archive.suggestedFilename(), /\.orpreset$/);
    const json = JSON.parse(await readFile(path, 'utf8'));
    assert.equal(json.name, 'Portable wind');
    assert.equal(json.effects.find(effect => effect.id === 'grain').enabled, false);
    const input = page.locator('#preset-input');
    await presets.selectOption('mystic-cathedral');
    await input.setInputFiles(path);
    await page.getByText('Imported Portable wind.').waitFor();
    assert.equal(await presets.inputValue(), json.id);
    assert.equal(await page.locator('[data-parameter=breathPressure]').getAttribute('aria-valuenow'), String(json.parameters.breathPressure));
    await input.setInputFiles({ name: 'invalid.orpreset', mimeType: 'application/json', buffer: Buffer.from('{"schemaVersion":2}') });
    await page.getByText('This preset schema version is unsupported.').waitFor();
    assert.equal(await presets.inputValue(), json.id);
    await page.reload();
    await presets.locator('option', { hasText: 'Portable wind' }).waitFor({ state: 'attached' });
  } finally {
    await browser.close();
    server.close();
  }
});
