import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { test } from 'node:test';
import { chromium } from 'playwright-core';
import { readMidi, writeMidi } from '../../docs/assets/midi-file.js';
import { startServer } from '../server.mjs';

test('standard MIDI round trips velocity, pitch, timing, tempo, and trailing bars', () => {
  const notes = [{ key: 60, start: 0, end: 960, velocity: 72 }, { key: 72, start: 480, end: 1440, velocity: 106 }];
  const data = writeMidi(notes, 123.45, 4);
  assert.deepEqual(readMidi(data), { notes, bpm: 123.45, bars: 4 });
  assert.throws(() => readMidi(data.slice(0, -2)), /Truncated/);
});

test('phone piano roll edits, plays, rewinds, relayouts, and exchanges MIDI files', async () => {
  const { server, address } = await startServer(0);
  const browser = await chromium.launch({ executablePath: process.env.OCARINA_BROWSER || undefined });
  try {
    const page = await browser.newPage({ viewport: { width: 393, height: 850 }, hasTouch: true, isMobile: true });
    page.setDefaultTimeout(10000);
    await page.goto(address);
    await page.locator('#sequencer summary').click();
    await page.locator('#roll-full').click();
    assert.equal(await page.locator('#roll-full').getAttribute('aria-pressed'), 'true');
    await page.locator('#roll-scroll').evaluate(element => { element.scrollTop = (127 - 67) * 24; });
    await page.getByLabel('Snap', { exact: true }).selectOption('960');
    const grid = page.locator('#roll-grid');
    const position = await grid.boundingBox();
    const x = position.x + 12, y = position.y + (127 - 60) * 24 + 12;
    await page.mouse.click(x, y);
    const selected = page.getByRole('button', { name: /^C4, beat 1.00/ });
    assert.equal(await selected.count(), 1);
    await page.getByLabel('Velocity', { exact: true }).fill('85');
    await page.getByLabel('Velocity', { exact: true }).press('Tab');
    assert.match(await selected.getAttribute('aria-label'), /85$/);
    const resize = await selected.locator('.roll-resize').boundingBox();
    await page.mouse.move(resize.x + resize.width / 2, resize.y + 8);
    await page.mouse.down();
    await page.mouse.move(position.x + 132, resize.y + 8, { steps: 5 });
    await page.mouse.up();
    assert.ok((await selected.boundingBox()).width >= 127);

    await page.getByRole('button', { name: 'Play', exact: true }).click();
    await page.getByText('Playing', { exact: true }).waitFor();
    await page.waitForFunction(() => document.querySelector('meter').value > .025);
    await page.getByRole('button', { name: 'Stop', exact: true }).click();
    await page.getByText('Stopped', { exact: true }).waitFor();
    await page.waitForFunction(() => document.querySelector('#voices').textContent === '0');
    await page.getByRole('button', { name: 'Rewind', exact: true }).click();
    await page.waitForFunction(() => document.querySelector('#roll-position').textContent === '1.1');
    await page.setViewportSize({ width: 850, height: 393 });
    assert.equal(await selected.count(), 1);

    const writing = page.waitForEvent('download');
    await page.getByRole('button', { name: 'Export MIDI', exact: true }).click();
    const download = await writing;
    const path = await download.path();
    assert.deepEqual(readMidi(await readFile(path)).notes, [{ key: 60, start: 0, end: 1920, velocity: 85 }]);
    await page.getByRole('button', { name: 'Clear', exact: true }).click();
    assert.equal(await selected.count(), 0);
    await page.locator('#roll-file').setInputFiles(path);
    await page.getByText('Imported 1 MIDI notes.').waitFor();
    assert.equal(await selected.count(), 1);
    await page.locator('#roll-full').click();
    await page.reload();
    await page.locator('#sequencer summary').click();
    assert.equal(await selected.count(), 1);
    await page.locator('#keyboard-full').click();
    assert.equal(await page.locator('#keyboard-full').getAttribute('aria-pressed'), 'true');
    await page.keyboard.press('Escape');
    assert.equal(await page.locator('#keyboard-full').getAttribute('aria-pressed'), 'false');
  } finally {
    await browser.close();
    server.close();
  }
});

test('typing, Web MIDI chords, CC learn, and source-specific disconnect use the real worklet', async () => {
  const { server, address } = await startServer(0);
  const browser = await chromium.launch({ executablePath: process.env.OCARINA_BROWSER || undefined });
  try {
    const page = await browser.newPage();
    page.setDefaultTimeout(10000);
    await page.addInitScript(() => {
      const ports = ['first', 'second'].map(id => ({ id, state: 'connected' }));
      const access = { inputs: new Map(ports.map(port => [port.id, port])) };
      navigator.requestMIDIAccess = async () => access;
      window.qaMidi = (index, message) => ports[index].onmidimessage?.({ data: Uint8Array.from(message) });
      window.qaDisconnect = index => { ports[index].state = 'disconnected'; access.onstatechange?.(); };
    });
    await page.goto(address);
    await page.locator('#midi-panel summary').click();
    await page.getByRole('button', { name: 'Enable Web MIDI' }).click();
    await page.getByText(/^Audio ready/).waitFor();
    await page.getByText('2 MIDI inputs connected.').waitFor();
    await page.getByRole('button', { name: 'Learn CC' }).click();
    await page.evaluate(() => window.qaMidi(0, [0xb0, 21, 99]));
    await page.getByText('CC 21 controls Breath Pressure.').waitFor();
    assert.equal(await page.locator('[data-parameter=breathPressure]').getAttribute('aria-valuenow'), '77.95');
    await page.evaluate(() => {
      window.qaMidi(0, [0x90, 60, 80]);
      window.qaMidi(0, [0xb0, 64, 127]);
      window.qaMidi(0, [0x80, 60, 0]);
      window.qaMidi(1, [0x91, 64, 95]);
    });
    await page.waitForFunction(() => document.querySelectorAll('.key[data-midi]').length === 2);
    await page.evaluate(() => window.qaDisconnect(0));
    await page.waitForFunction(() => [...document.querySelectorAll('.key[data-midi]')].map(el => el.ariaLabel).join() === 'E4');
    await page.evaluate(() => window.qaMidi(1, [0x81, 64, 0]));
    await page.waitForFunction(() => document.querySelectorAll('.key[data-midi]').length === 0);
    await page.locator('body').evaluate(element => { element.tabIndex = -1; element.focus(); });
    await page.keyboard.down('a');
    await page.waitForFunction(() => document.querySelector('#note').textContent === 'C4');
    await page.keyboard.up('a');
    await page.waitForFunction(() => document.querySelector('#voices').textContent === '0');
  } finally {
    await browser.close();
    server.close();
  }
});
