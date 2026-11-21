import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const docs = fileURLToPath(new URL('../docs', import.meta.url));
const base = '/Ocarina-of-Resonance/';
const types = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.webp': 'image/webp',
  '.wasm': 'application/wasm',
};

export function startServer(port = 8080) {
  const server = createServer(async (request, response) => {
    const path = new URL(request.url, 'http://localhost').pathname;
    if (path === '/') {
      response.writeHead(302, { Location: base }).end();
      return;
    }
    if (!path.startsWith(base)) {
      response.writeHead(404).end();
      return;
    }
    try {
      const file = resolve(docs, decodeURIComponent(path.slice(base.length)) || 'index.html');
      if (!file.startsWith(docs + sep) || !Object.hasOwn(types, extname(file))) throw new Error('Invalid path');
      const bytes = await readFile(file);
      response.writeHead(200, { 'Content-Type': types[extname(file)], 'Cache-Control': 'no-store' }).end(bytes);
    } catch {
      response.writeHead(404).end();
    }
  });
  return new Promise((resolveServer, reject) => {
    server.once('error', reject);
    server.listen(port, '127.0.0.1', () => {
      const address = `http://127.0.0.1:${server.address().port}${base}`;
      resolveServer({ server, address });
    });
  });
}

if (resolve(process.argv[1] ?? '') === fileURLToPath(import.meta.url)) {
  const { address } = await startServer(Number(process.argv[2] ?? 8080));
  console.log(address);
}
