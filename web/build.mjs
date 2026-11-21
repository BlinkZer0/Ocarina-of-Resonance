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
  '-std=c++20', '-O3', '--no-entry', '-sSTANDALONE_WASM=1', '-sALLOW_MEMORY_GROWTH=0', '-sINITIAL_MEMORY=33554432',
  '-I', join(root, 'src'), ...sources, join(web, 'wasm.cpp'),
  '-o', join(assets, 'ocarina.wasm'),
]);
run(process.execPath, [join(web, 'node_modules', 'typescript', 'bin', 'tsc')]);

const version = readFileSync(join(root, 'VERSION'), 'utf8').trim();
const html = readFileSync(join(web, 'index.html'), 'utf8').replaceAll('%VERSION%', version);
writeFileSync(join(publish, 'index.html'), html);
copyFileSync(join(web, 'style.css'), join(assets, 'style.css'));
copyFileSync(join(root, 'ui', 'ocarina.webp'), join(assets, 'ocarina.webp'));
const registry = readFileSync(join(root, 'src', 'shared', 'Parameters.h'), 'utf8');
const controls = Array.from(registry.matchAll(/\{"([\w]+)", "([^"]+)", "([^"]*)", (-?[\d.]+)f, (-?[\d.]+)f, (-?[\d.]+)f\}/g),
  ([, id, name, unit, min, max, initial], index) => ({ index, id, name, unit, min: Number(min), max: Number(max), initial: Number(initial) }));
const order = registry.match(/enum ParameterIndex : size_t \{([^}]+)\}/)?.[1].split(',').map(id => id.trim()).slice(0, -1);
if (!order || controls.length !== order.length || controls.some(({ id }, index) => (index === 0 ? 'master' : id) !== order[index])) {
  throw new Error('The UI parameter manifest does not match the C++ registry.');
}
const models = [...(registry.match(/modelNames \{([^}]+)\}/)?.[1] ?? '').matchAll(/"([^"]+)"/g)].map(([, model]) => model);
if (models.length !== 4) throw new Error('Could not read the sound models from the C++ registry.');
const face = JSON.parse(readFileSync(join(root, 'ui', 'holes.json'), 'utf8'));
if (!(face.width > 0 && face.height > 0) || face.centers.length !== 12
    || face.centers.some(point => point.length !== 2 || point.some(value => !(value > 0 && value < 100)))) {
  throw new Error('The instrument artwork needs twelve valid finger-hole centers.');
}
const effects = JSON.parse(readFileSync(join(root, 'presets', 'effects.json'), 'utf8'));
const { PresetFormat } = await import(new URL('../docs/assets/presets.js', import.meta.url));
const format = new PresetFormat(controls, effects);
const factory = JSON.parse(readFileSync(join(root, 'presets', 'factory.json'), 'utf8')).map(preset => format.read(preset));
if (factory.length !== 40 || new Set(factory.map(preset => preset.id)).size !== 40
    || ['Authentic', 'Retro', 'Electronic', 'Cinematic'].some(bank => factory.filter(preset => preset.category === bank).length !== 10)) {
  throw new Error('Factory library must contain four banks of ten unique presets.');
}
writeFileSync(join(assets, 'parameters.json'), JSON.stringify({ controls, models, face, effects, factory }, null, 2) + '\n');
writeFileSync(join(root, 'presets', 'factory-vectors.tsv'), factory.map(preset => [preset.id, ...format.values(preset)].join('\t')).join('\n') + '\n');
writeFileSync(join(publish, '.nojekyll'), '');
console.log(`Built docs/ for Ocarina of Resonance ${version}`);
