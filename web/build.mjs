import { spawnSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const web = dirname(fileURLToPath(import.meta.url));
const root = dirname(web);
const publish = join(root, 'docs');
const assets = join(publish, 'assets');
const sdk = process.env.EMSDK ?? (process.env.LOCALAPPDATA && join(process.env.LOCALAPPDATA, 'EightBallVST', 'emsdk'));
const windows = process.platform === 'win32';
const cachedCompiler = sdk && join(sdk, 'upstream', 'emscripten', windows ? 'em++.exe' : 'em++');
const compiler = process.env.OCARINA_EMXX ?? (cachedCompiler && existsSync(cachedCompiler) ? cachedCompiler : 'em++');

function run(command, args) {
  const result = spawnSync(command, args, { cwd: web, stdio: 'inherit', env: { ...process.env, EMSDK: sdk } });
  if (result.error) throw result.error;
  if (result.status !== 0) process.exit(result.status ?? 1);
}

mkdirSync(assets, { recursive: true });
const dsp = join(root, 'src', 'dsp');
const sources = readdirSync(dsp).filter(name => name.endsWith('.cpp')).sort().map(name => join(dsp, name));
run(compiler, [
  '-std=c++20', '-O3', '--no-entry', '-sSTANDALONE_WASM=1', '-sALLOW_MEMORY_GROWTH=0',
  '-I', join(root, 'src'), ...sources, join(web, 'wasm.cpp'),
  '-o', join(assets, 'ocarina.wasm'),
]);
run(process.execPath, [join(web, 'node_modules', 'typescript', 'bin', 'tsc')]);

const version = readFileSync(join(root, 'VERSION'), 'utf8').trim();
const html = readFileSync(join(web, 'index.html'), 'utf8').replaceAll('%VERSION%', version);
writeFileSync(join(publish, 'index.html'), html);
copyFileSync(join(web, 'style.css'), join(assets, 'style.css'));
writeFileSync(join(publish, '.nojekyll'), '');
console.log(`Built docs/ for Ocarina of Resonance ${version}`);
