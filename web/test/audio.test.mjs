import assert from 'node:assert/strict';
import { test } from 'node:test';
import { chromium } from 'playwright-core';
import { startServer } from '../server.mjs';

test('plays and releases a Wasm voice under the Pages project subpath', async () => {
  const { server, address } = await startServer(0);
  try {
    const browser = await chromium.launch({ executablePath: process.env.OCARINA_BROWSER || undefined });
    try {
      const page = await browser.newPage();
      const errors = [];
      page.on('pageerror', (error) => errors.push(error.message));
      await page.goto(address);
      assert.equal(await page.title(), 'Ocarina of Resonance');
      await page.getByLabel('Sound model').selectOption('2');
      await page.getByLabel('Polyphony').selectOption('0');
      await page.getByRole('button', { name: 'Start audio' }).click();
      await page.getByText(/^Audio ready/).waitFor();
      const key = page.getByRole('button', { name: 'A4' });
      await key.focus();
      await page.keyboard.down('Space');
      await page.waitForFunction(() => document.querySelector('meter').value > 0.1);
      await page.keyboard.up('Space');
      await page.waitForFunction(() => document.querySelector('meter').value === 0);

      const bounds = await key.boundingBox();
      await page.mouse.move(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
      await page.mouse.down();
      await page.waitForFunction(() => document.querySelector('meter').value > 0.1);
      await page.mouse.move(0, 0);
      await page.mouse.up();
      await page.waitForFunction(() => document.querySelector('meter').value === 0);
      assert.deepEqual(errors, []);
    } finally {
      await browser.close();
    }
  } finally {
    server.close();
  }
});

test('starts with a cached panel that predates the extra Android diagnostics', async (t) => {
  const { server, address } = await startServer(0);
  t.after(() => server.close());
  const browser = await chromium.launch({ executablePath: process.env.OCARINA_BROWSER || undefined });
  t.after(() => browser.close());
  const page = await browser.newPage();
  await page.route(address, async route => {
    const response = await route.fetch();
    const body = (await response.text()).replace(/<button id="profile-(?:extended|stop)"[^>]*>[^<]*<\/button>/g, '');
    await route.fulfill({ response, body });
  });
  await page.goto(address);
  assert.equal(await page.locator('#profile-extended, #profile-stop').count(), 0);
  await page.getByRole('button', { name: 'Start audio' }).click();
  await page.getByText(/^Audio ready/).waitFor();
});
