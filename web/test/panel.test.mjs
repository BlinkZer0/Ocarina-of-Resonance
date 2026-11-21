import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { test } from 'node:test';
import { chromium } from 'playwright-core';
import { startServer } from '../server.mjs';

const layout = JSON.parse(await readFile(new URL('../../ui/holes.json', import.meta.url), 'utf8'));

test('the ocarina and thumb controls send all twelve values and shape the sounding voice', async () => {
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
    assert.equal(await page.locator('#holes .face-hole').count(), 10);
    assert.equal(await page.locator('#holes .subhole').count(), 2);
    assert.deepEqual(await page.locator('#thumb-controls .thumb-hole').evaluateAll(
      buttons => buttons.map(button => button.dataset.parameter)), ['attackArticulation', 'formantColor']);
    const aligned = await page.evaluate(async front => {
      const art = document.querySelector('#art');
      await art.decode();
      const canvas = document.createElement('canvas');
      canvas.width = art.naturalWidth;
      canvas.height = art.naturalHeight;
      const context = canvas.getContext('2d');
      context.drawImage(art, 0, 0);
      const bounds = art.getBoundingClientRect();
      return [...document.querySelectorAll('.face-hole')].every(button => {
        const box = button.querySelector('.ring').getBoundingClientRect();
        const x = (box.x + box.width / 2 - bounds.x) / bounds.width;
        const y = (box.y + box.height / 2 - bounds.y) / bounds.height;
        const position = front[button.dataset.parameter];
        const [r, g, b, a] = context.getImageData(x * canvas.width, y * canvas.height, 1, 1).data;
        return Math.abs(x * 100 - position.x) < .1 && Math.abs(y * 100 - position.y) < .1
          && a > 245 && b > Math.max(r, g) * 1.4;
      });
    }, layout.front);
    assert.ok(aligned, 'the ten front openings stay on the blue face at their mapped positions');
    const stage = await page.locator('#stage').boundingBox();
    const thumb = await page.locator('.thumb-hole').first().boundingBox();
    assert.ok(thumb.y >= stage.y + stage.height, 'thumb controls are below the ocarina');
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

    for (const viewport of [{ width: 320, height: 680 }, { width: 393, height: 850 },
      { width: 850, height: 393 }, { width: 820, height: 1080 }]) {
      await page.setViewportSize(viewport);
      const stage = await page.locator('.stage-shell').evaluate(element => new Promise(resolve => {
        requestAnimationFrame(() => requestAnimationFrame(() => resolve(element.getBoundingClientRect().toJSON())));
      }));
      const targets = await holes.evaluateAll(buttons => buttons.map(button => {
        const bounds = button.getBoundingClientRect();
        return { id: button.dataset.parameter, front: button.classList.contains('face-hole'),
          x: bounds.x, y: bounds.y, width: bounds.width, height: bounds.height };
      }));
      for (const [index, a] of targets.entries()) {
        assert.ok(a.width >= 44 && a.height >= 44, a.id);
        if (a.front) assert.ok(a.x >= stage.left - 1 && a.x + a.width <= stage.right + 1,
          `Full touch area visible for ${a.id} at ${viewport.width} × ${viewport.height}`);
        assert.ok(targets.slice(index + 1).every(b =>
          a.x + a.width <= b.x || b.x + b.width <= a.x || a.y + a.height <= b.y || b.y + b.height <= a.y),
        `Distinct touch area for ${a.id} at ${viewport.width} × ${viewport.height}`);
      }
    }
  } finally {
    await browser.close();
    server.close();
  }
});
