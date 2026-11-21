import assert from 'node:assert/strict';
import { test } from 'node:test';
import { chromium } from 'playwright-core';
import { startServer } from '../server.mjs';

test('finger holes send all twelve values and shape the sounding voice', async () => {
  const { server, address } = await startServer(0);
  const browser = await chromium.launch({ executablePath: process.env.OCARINA_BROWSER || undefined });
  try {
    const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
    await page.addInitScript(() => {
      window.parameterEvents = [];
      const post = MessagePort.prototype.postMessage;
      MessagePort.prototype.postMessage = function (message, ...args) {
        if (Number.isInteger(message?.index)) window.parameterEvents.push(message.index);
        return post.call(this, message, ...args);
      };
    });
    await page.goto(address);
    await page.getByRole('button', { name: 'Start audio' }).click();
    await page.getByText(/^Audio ready/).waitFor();
    await page.evaluate(() => { window.parameterEvents.length = 0; });
    const holes = page.locator('.hole');
    assert.equal(await holes.count(), 12);
    const aligned = await page.evaluate(async () => {
      const art = document.querySelector('#art');
      await art.decode();
      const canvas = document.createElement('canvas');
      canvas.width = art.naturalWidth;
      canvas.height = art.naturalHeight;
      const context = canvas.getContext('2d');
      context.drawImage(art, 0, 0);
      const bounds = art.getBoundingClientRect();
      return [...document.querySelectorAll('.ring')].every(ring => {
        const box = ring.getBoundingClientRect();
        const x = (box.x + box.width / 2 - bounds.x) / bounds.width * canvas.width;
        const y = (box.y + box.height / 2 - bounds.y) / bounds.height * canvas.height;
        const [r, g, b, a] = context.getImageData(x, y, 1, 1).data;
        return a > 245 && r < 15 && g < 20 && b < 42;
      });
    });
    assert.ok(aligned, 'every knob is centered over a recessed opening in the artwork');
    for (const hole of await holes.all()) {
      const initial = await hole.getAttribute('aria-valuenow');
      await hole.press('ArrowUp');
      assert.ok(Number(await hole.getAttribute('aria-valuenow')) > Number(initial));
    }
    assert.deepEqual(await page.evaluate(() => window.parameterEvents), Array.from({ length: 12 }, (_, i) => i + 1));

    const breath = holes.first();
    await breath.dblclick();
    assert.equal(await breath.getAttribute('aria-valuenow'), '55');
    await breath.hover();
    await page.mouse.wheel(0, -120);
    assert.ok(Number(await breath.getAttribute('aria-valuenow')) > 55);
    await breath.dblclick();
    const box = await breath.boundingBox();
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2 - 32);
    await page.mouse.up();
    assert.ok(Number(await breath.getAttribute('aria-valuenow')) > 65);

    await page.getByRole('button', { name: 'A4' }).focus();
    await page.keyboard.down('Space');
    await page.waitForFunction(() => document.querySelector('meter').value > .08);
    await breath.dispatchEvent('keydown', { key: 'Home' });
    await page.waitForFunction(() => document.querySelector('meter').value < .003);
    assert.equal(await page.locator('#note').textContent(), 'A4');
    await page.keyboard.up('Space');
    await page.waitForFunction(() => document.querySelector('#note').textContent === '—');
  } finally {
    await browser.close();
    server.close();
  }
});

test('phone layout supports independent touches and a separate keyboard scroll control', async () => {
  const { server, address } = await startServer(0);
  const browser = await chromium.launch({ executablePath: process.env.OCARINA_BROWSER || undefined });
  try {
    const context = await browser.newContext({ viewport: { width: 393, height: 850 }, hasTouch: true, isMobile: true });
    const page = await context.newPage();
    await page.goto(address);
    const holes = page.locator('.hole');
    await holes.last().waitFor();
    const first = await holes.nth(0).boundingBox(), second = await holes.nth(1).boundingBox();
    assert.ok(first.width >= 44 && first.height >= 44 && second.x >= first.x + first.width);
    const client = await context.newCDPSession(page);
    await page.getByRole('checkbox', { name: 'Fine' }).check();
    const p1 = { x: first.x + first.width / 2, y: first.y + first.height / 2, id: 1 };
    const p2 = { x: second.x + second.width / 2, y: second.y + second.height / 2, id: 2 };
    const touch = (type, touchPoints) => client.send('Input.dispatchTouchEvent', { type, touchPoints });
    await touch('touchStart', [p1]);
    await touch('touchStart', [p1, p2]);
    await touch('touchMove', [{ ...p1, y: p1.y - 33 }, { ...p2, y: p2.y + 28 }]);
    await touch('touchEnd', []);
    const breath = Number(await holes.nth(0).getAttribute('aria-valuenow'));
    const jet = Number(await holes.nth(1).getAttribute('aria-valuenow'));
    assert.ok(breath > 55 && breath < 61 && jet < 50 && jet > 44);
    assert.equal(await page.evaluate(() => window.scrollY), 0);
    assert.equal(await page.locator('#keys .key').count(), 48);
    const scroll = page.getByRole('slider', { name: 'Scroll keyboard' });
    await scroll.fill('1000');
    assert.ok(await page.locator('.key-scroll').evaluate(element => element.scrollLeft > 0));
  } finally {
    await browser.close();
    server.close();
  }
});
