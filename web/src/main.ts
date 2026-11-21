import type { VoiceMessage } from './worklet.js';

type Control = { index: number; id: string; name: string; unit: string; min: number; max: number; initial: number };
type Catalogue = { controls: Control[]; models: string[]; face: { width: number; height: number; centers: [number, number][] } };
type Telemetry = { peak?: number; voices?: number; note?: number; params?: number[]; status?: string; debug?: boolean; diagnostic?: string };
type Request = { type: 'ready' | 'release' | 'test' | 'profile' }
  | { type: 'begin' | 'end'; index: number }
  | { type: 'parameter'; index: number; value: number }
  | { type: 'note'; key: number; source: number; on: boolean };

declare global {
  interface Window {
    __JUCE__?: { backend: {
      emitEvent(id: string, message: Request): void;
      addEventListener(id: string, receive: (data: Telemetry) => void): void;
    } };
    AndroidNative?: { message(text: string): void };
    ocarinaReceive?: (data: Telemetry) => void;
  }
}

const get = <T extends HTMLElement>(id: string) => document.getElementById(id)! as T;
const start = get<HTMLButtonElement>('start');
const status = get('status');
const keyboard = get<HTMLFieldSetElement>('keys');
const scroll = get<HTMLInputElement>('keyboard-scroll');
const keysWindow = keyboard.parentElement!;
const stage = get('stage');
const meter = get<HTMLMeterElement>('meter');
const model = get<HTMLSelectElement>('model');
const polyphony = get<HTMLSelectElement>('polyphony');
const fine = get<HTMLInputElement>('fine');
const numeric = get<HTMLInputElement>('hole-value');
const desktop = window.__JUCE__?.backend;
const android = window.AndroidNative;
let context: AudioContext | undefined;
let worklet: AudioWorkletNode | undefined;
let controls: Control[];
let values: number[];
let selected: Control;
let fromNote = 48, midiNote = -1;
const bindings = new Map<number, (value: number) => void>();
const editing = new Set<number>();
const holes = new Map<number, HTMLButtonElement>();
const piano = new Map<number, HTMLButtonElement>();
const playing = new Map<string, { key: number; source: number }>();
const pitches = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];
const noteName = (note: number) => pitches[note % 12] + (Math.floor(note / 12) - 1);
const compact = (value: number) => String(Number(value.toFixed(2)));

function send(message: Request): void {
  if (desktop) desktop.emitEvent('instrumentInput', message);
  else if (android) android.message(JSON.stringify(message));
  else if (worklet) {
    let payload: VoiceMessage;
    if (message.type === 'parameter') payload = { index: message.index, value: message.value };
    else if (message.type === 'note') payload = { key: message.key, source: message.source, on: message.on };
    else if (message.type === 'release') payload = { release: true };
    else return;
    worklet.port.postMessage(payload);
  }
}

function asset(name: string): URL {
  const moduleUrl = new URL(import.meta.url);
  const resource = new URL(name, moduleUrl);
  resource.search = moduleUrl.search;
  return resource;
}

function paintTelemetry(update: Telemetry): void {
  if (update.params && values) {
    for (const control of controls) {
      const value = update.params[control.index];
      if (!editing.has(control.index) && Number.isFinite(value) && value !== values[control.index])
        paint(control, value);
    }
  }
  if (update.status !== undefined && status.textContent !== update.status) status.textContent = update.status;
  if (update.debug) get('diagnostics').hidden = false;
  if (update.diagnostic !== undefined) get('diagnostic-result').textContent = update.diagnostic;
  if (update.peak !== undefined) {
    meter.value = update.peak;
    get('level').textContent = update.peak > .0001 ? Math.round(20 * Math.log10(update.peak)) + ' dB' : '−∞ dB';
    stage.style.setProperty('--light', String(.37 + Math.min(1, update.peak * 4) * .63));
    stage.style.setProperty('--breathing', String(Math.min(.65, update.peak * 2)));
  }
  if (update.voices !== undefined) get('voices').textContent = String(update.voices);
  if (update.note !== undefined && update.note !== midiNote) {
    piano.get(midiNote)?.removeAttribute('data-midi');
    midiNote = update.note;
    get('note').textContent = midiNote < 0 ? '—' : noteName(midiNote);
    piano.get(midiNote)?.setAttribute('data-midi', 'true');
  }
}

function paint(control: Control, value: number): void {
  values[control.index] = value;
  bindings.get(control.index)?.(value);
  if (selected?.index === control.index && document.activeElement !== numeric)
    numeric.value = compact(value);
}

function setValue(control: Control, value: number): void {
  if (!Number.isFinite(value)) return;
  const next = Math.min(control.max, Math.max(control.min, Number(value.toFixed(2))));
  if (values[control.index] === next) return;
  paint(control, next);
  send({ type: 'parameter', index: control.index, value: next });
}

function change(control: Control, value: number): void {
  send({ type: 'begin', index: control.index });
  setValue(control, value);
  send({ type: 'end', index: control.index });
}

function select(control: Control): void {
  holes.get(selected?.index)?.removeAttribute('data-selected');
  selected = control;
  holes.get(control.index)?.setAttribute('data-selected', 'true');
  get('hole-name').textContent = control.name;
  get('hole-unit').textContent = control.unit;
  numeric.min = String(control.min);
  numeric.max = String(control.max);
  numeric.value = compact(values[control.index]);
}

function makeHole(control: Control, label: string, [x, y]: [number, number]): void {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'hole';
  button.dataset.parameter = control.id;
  button.setAttribute('role', 'slider');
  button.setAttribute('aria-label', control.name);
  button.setAttribute('aria-valuemin', String(control.min));
  button.setAttribute('aria-valuemax', String(control.max));
  button.style.setProperty('--x', x + '%');
  button.style.setProperty('--y', y + '%');

  const ring = document.createElement('span');
  ring.className = 'ring';
  const engraving = document.createElement('span');
  engraving.className = 'engraving';
  engraving.textContent = label;
  button.append(ring, engraving);
  holes.set(control.index, button);
  get('holes').append(button);

  bindings.set(control.index, (value) => {
    const valueText = (compact(value) + ' ' + control.unit).trim();
    button.setAttribute('aria-valuenow', compact(value));
    button.setAttribute('aria-valuetext', valueText);
    button.title = control.name + ' · ' + valueText;
    ring.style.setProperty('--turn', (270 * (value - control.min) / (control.max - control.min)) + 'deg');
  });
  paint(control, values[control.index]);

  let active: number | undefined;
  let previousY = 0;
  button.addEventListener('focus', () => select(control));
  button.addEventListener('pointerdown', event => {
    if (event.button !== 0 || active !== undefined) return;
    select(control);
    active = event.pointerId;
    previousY = event.clientY;
    button.setPointerCapture(active);
    editing.add(control.index);
    send({ type: 'begin', index: control.index });
  });
  button.addEventListener('pointermove', event => {
    if (event.pointerId !== active) return;
    const sensitivity = fine.checked || event.shiftKey ? 950 : 185;
    setValue(control, values[control.index] + (previousY - event.clientY) * (control.max - control.min) / sensitivity);
    previousY = event.clientY;
  });
  const end = (event: PointerEvent) => {
    if (event.pointerId !== active) return;
    active = undefined;
    editing.delete(control.index);
    send({ type: 'end', index: control.index });
  };
  button.addEventListener('pointerup', end);
  button.addEventListener('pointercancel', end);
  button.addEventListener('lostpointercapture', end);
  button.addEventListener('dblclick', () => { select(control); change(control, control.initial); });
  button.addEventListener('wheel', event => {
    if (event.ctrlKey || event.deltaY === 0) return;
    event.preventDefault();
    select(control);
    const scale = fine.checked || event.shiftKey ? .0025 : .015;
    change(control, values[control.index] - Math.sign(event.deltaY) * (control.max - control.min) * scale);
  }, { passive: false });
  button.addEventListener('keydown', event => {
    const steps: Record<string, number> = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 };
    let value = values[control.index];
    if (event.key === 'Home') value = control.min;
    else if (event.key === 'End') value = control.max;
    else if (steps[event.key]) value += steps[event.key] * (control.max - control.min) * (event.shiftKey || fine.checked ? .0025 : .01);
    else return;
    event.preventDefault();
    change(control, value);
  });
}

function makeRange(control: Control, parent: HTMLElement): void {
  const label = document.createElement('label');
  const caption = document.createElement('span');
  caption.className = 'range-top';
  const name = document.createElement('span');
  name.textContent = control.name;
  const output = document.createElement('output');
  caption.append(name, output);
  const input = document.createElement('input');
  input.type = 'range';
  input.min = String(control.min);
  input.max = String(control.max);
  input.step = 'any';
  input.setAttribute('aria-label', control.name);
  label.append(caption, input);
  parent.append(label);
  bindings.set(control.index, value => {
    if (document.activeElement !== input || !editing.has(control.index)) input.value = String(value);
    output.textContent = (compact(value) + ' ' + control.unit).trim();
  });
  paint(control, values[control.index]);
  input.addEventListener('pointerdown', () => {
    editing.add(control.index);
    send({ type: 'begin', index: control.index });
  });
  const end = () => {
    if (editing.delete(control.index)) send({ type: 'end', index: control.index });
  };
  input.addEventListener('change', end);
  input.addEventListener('pointerup', end);
  input.addEventListener('pointercancel', end);
  input.addEventListener('lostpointercapture', end);
  input.addEventListener('blur', end);
  input.addEventListener('input', () => {
    if (editing.has(control.index)) setValue(control, input.valueAsNumber);
    else change(control, input.valueAsNumber);
  });
}

function keyDown(id: string, key: number): void {
  if (playing.has(id)) return;
  const first = ![...playing.values()].some(held => held.key === key);
  playing.set(id, { key, source: 2 });
  piano.get(key)?.setAttribute('data-down', 'true');
  if (first) send({ type: 'note', key, source: 2, on: true });
}
function keyUp(id: string): void {
  const note = playing.get(id);
  if (!note) return;
  playing.delete(id);
  if (![...playing.values()].some(held => held.key === note.key)) {
    send({ type: 'note', ...note, on: false });
    piano.get(note.key)?.removeAttribute('data-down');
  }
}
function releasePiano(): void {
  playing.clear();
  for (const key of piano.values()) key.removeAttribute('data-down');
  send({ type: 'release' });
}

function buildPiano(): void {
  releasePiano();
  piano.clear();
  keyboard.replaceChildren();
  let white = 0;
  for (let key = fromNote; key < fromNote + 48; ++key) {
    const name = noteName(key);
    const black = name.includes('♯');
    const button = document.createElement('button');
    button.type = 'button';
    button.className = black ? 'key black' : 'key';
    button.setAttribute('aria-label', name);
    button.style.setProperty('--left', String(black ? white - .315 : white++));
    if (key === midiNote) button.dataset.midi = 'true';
    if (key % 12 === 0 || key === 69) button.textContent = name;
    piano.set(key, button);
    keyboard.append(button);
    button.addEventListener('pointerdown', event => {
      if (event.button !== 0) return;
      button.setPointerCapture(event.pointerId);
      keyDown('p' + event.pointerId, key);
    });
    for (const type of ['pointerup', 'pointercancel', 'lostpointercapture'] as const)
      button.addEventListener(type, event => keyUp('p' + event.pointerId));
    button.addEventListener('keydown', event => {
      if (event.key !== ' ' && event.key !== 'Enter') return;
      event.preventDefault();
      keyDown('focus', key);
    });
    button.addEventListener('keyup', event => {
      if (event.key !== ' ' && event.key !== 'Enter') return;
      event.preventDefault();
      keyUp('focus');
    });
    button.addEventListener('blur', () => keyUp('focus'));
  }
  get('range').textContent = noteName(fromNote) + '–' + noteName(fromNote + 47);
  get<HTMLButtonElement>('lower').disabled = fromNote === 12;
  get<HTMLButtonElement>('higher').disabled = fromNote === 72;
  keysWindow.scrollLeft = 0;
  syncKeyboardScroll();
}

function syncKeyboardScroll(): void {
  const track = keysWindow.scrollWidth - keysWindow.clientWidth;
  scroll.hidden = track <= 0;
  scroll.value = String(track > 0 ? Math.round(1000 * keysWindow.scrollLeft / track) : 0);
}

async function startAudio(): Promise<void> {
  start.disabled = true;
  status.textContent = 'Starting audio…';
  let next: AudioContext | undefined;
  try {
    if (!window.AudioContext || !window.isSecureContext || !window.WebAssembly)
      throw new Error('Use a current browser over HTTPS or localhost to play.');
    next = new AudioContext({ latencyHint: 'interactive' });
    await next.resume();
    const [response] = await Promise.all([fetch(asset('./ocarina.wasm')), next.audioWorklet.addModule(asset('./worklet.js'))]);
    if (!response.ok) throw new Error('Audio module unavailable (' + response.status + ').');
    const module = await WebAssembly.compile(await response.arrayBuffer());
    const node = new AudioWorkletNode(next, 'ocarina-voice', {
      numberOfInputs: 0, outputChannelCount: [2], processorOptions: { module },
    });
    node.port.onmessage = ({ data }: MessageEvent<Telemetry>) => paintTelemetry(data);
    node.onprocessorerror = () => {
      worklet = undefined;
      keyboard.disabled = true;
      start.disabled = false;
      start.hidden = false;
      releasePiano();
      paintTelemetry({ peak: 0, note: -1, voices: 0, status: 'Audio stopped. Select Start audio to retry.' });
      void context?.close();
    };
    node.connect(next.destination);
    context = next;
    worklet = node;
    for (const control of controls) send({ type: 'parameter', index: control.index, value: values[control.index] });
    keyboard.disabled = false;
    start.hidden = true;
    status.textContent = 'Audio ready · ' + context.sampleRate + ' Hz';
  } catch (error) {
    if (next) await next.close();
    status.textContent = error instanceof Error ? error.message : 'Audio could not start.';
    start.disabled = false;
  }
}

async function initialise(): Promise<void> {
  const response = await fetch(asset('./parameters.json'));
  if (!response.ok) throw new Error('Could not load instrument controls.');
  const catalogue = await response.json() as Catalogue;
  controls = catalogue.controls;
  values = controls.map(control => control.initial);
  stage.style.setProperty('--aspect', String(catalogue.face.width / catalogue.face.height));
  const faceWindow = stage.parentElement!;
  new ResizeObserver(() => {
    faceWindow.scrollLeft = Math.max(0, (faceWindow.scrollWidth - faceWindow.clientWidth) / 2);
  }).observe(faceWindow);
  const engravings = ['Breath', 'Jet', 'Edge', 'Noise', 'Cavity', 'Aperture', 'Neck', 'Damp', 'Detune', 'Vibrato', 'Attack', 'Formant'];
  controls.slice(1, 13).forEach((control, index) => makeHole(control, engravings[index], catalogue.face.centers[index]));
  select(controls[1]);
  makeRange(controls[0], get('master'));
  const byId = (id: string) => controls.find(control => control.id === id)!;
  for (const id of ['breathCurve', 'turbulenceColor', 'excitationShape', 'harmonicEnhancement'])
    makeRange(byId(id), get('breath'));
  for (const id of ['portamento', 'velocityResponse', 'aftertouchResponse', 'bendRange'])
    makeRange(byId(id), get('articulation'));

  const soundModel = byId('model');
  catalogue.models.forEach((name, index) => model.add(new Option(name, String(index))));
  polyphony.add(new Option('Mono legato', '0'));
  for (let count = 1; count <= byId('voiceLimit').max; ++count)
    polyphony.add(new Option(count + ' voices', String(count)));
  model.addEventListener('change', () => change(soundModel, Number(model.value)));
  const voiceMode = byId('voiceMode'), voiceLimit = byId('voiceLimit');
  polyphony.addEventListener('change', () => {
    const count = Number(polyphony.value);
    if (count > 0) change(voiceLimit, count);
    change(voiceMode, count > 0 ? 1 : 0);
  });
  const syncPolyphony = () => { polyphony.value = String(values[voiceMode.index] ? values[voiceLimit.index] : 0); };
  bindings.set(soundModel.index, value => { model.value = String(value); });
  bindings.set(voiceMode.index, syncPolyphony);
  bindings.set(voiceLimit.index, syncPolyphony);
  syncPolyphony();
  if (matchMedia('(max-width: 720px)').matches)
    document.querySelectorAll<HTMLDetailsElement>('.rail').forEach(rail => { rail.open = false; });

  numeric.addEventListener('change', () => {
    change(selected, numeric.valueAsNumber);
    numeric.value = compact(values[selected.index]);
  });
  get('reset').addEventListener('click', () => change(selected, selected.initial));
  get('lower').addEventListener('click', () => { fromNote -= 12; buildPiano(); });
  get('higher').addEventListener('click', () => { fromNote += 12; buildPiano(); });
  scroll.addEventListener('input', () => {
    keysWindow.scrollLeft = scroll.valueAsNumber / 1000 * (keysWindow.scrollWidth - keysWindow.clientWidth);
  });
  keysWindow.addEventListener('scroll', syncKeyboardScroll);
  new ResizeObserver(syncKeyboardScroll).observe(keysWindow);
  get('test-tone').addEventListener('click', () => send({ type: 'test' }));
  get('profile').addEventListener('click', () => send({ type: 'profile' }));
  buildPiano();

  window.addEventListener('blur', releasePiano);
  document.addEventListener('visibilitychange', () => { if (document.hidden) releasePiano(); });
  if (desktop || android) {
    window.ocarinaReceive = paintTelemetry;
    desktop?.addEventListener('instrumentState', paintTelemetry);
    keyboard.disabled = false;
    start.hidden = true;
    send({ type: 'ready' });
  } else {
    start.disabled = false;
    start.addEventListener('click', () => void startAudio());
    status.textContent = '';
  }
}

void initialise().catch(() => { status.textContent = 'The instrument controls could not load.'; });
