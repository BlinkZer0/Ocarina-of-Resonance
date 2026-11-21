import type { VoiceMessage } from './worklet.js';

const start = document.querySelector<HTMLButtonElement>('#start')!;
const status = document.querySelector<HTMLParagraphElement>('#status')!;
const keys = document.querySelector<HTMLFieldSetElement>('#keys')!;
const meter = document.querySelector<HTMLMeterElement>('#meter')!;
let context: AudioContext | undefined;
let voice: AudioWorkletNode | undefined;

function post(message: VoiceMessage): void {
  voice?.port.postMessage(message);
}

start.addEventListener('click', async () => {
  start.disabled = true;
  status.textContent = 'Starting audio…';
  let nextContext: AudioContext | undefined;
  try {
    if (!window.AudioContext || !window.isSecureContext || !window.WebAssembly) {
      throw new Error('Use a current browser over HTTPS or localhost to play.');
    }
    nextContext = new AudioContext({ latencyHint: 'interactive' });
    const resume = nextContext.resume();
    // Start resuming in the gesture handler before fetching the worklet or Wasm.
    await resume;
    const [response] = await Promise.all([
      fetch(new URL('./ocarina.wasm', import.meta.url)),
      nextContext.audioWorklet.addModule(new URL('./worklet.js', import.meta.url)),
    ]);
    if (!response.ok) throw new Error(`Audio module unavailable (${response.status}).`);
    const module = await WebAssembly.compile(await response.arrayBuffer());
    const node = new AudioWorkletNode(nextContext, 'ocarina-voice', {
      numberOfInputs: 0, outputChannelCount: [2], processorOptions: { module },
    });
    node.port.onmessage = ({ data }: MessageEvent<number>) => { meter.value = data; };
    node.onprocessorerror = () => {
      voice = undefined;
      keys.disabled = true;
      start.disabled = false;
      start.hidden = false;
      meter.value = 0;
      status.textContent = 'Audio stopped. Select Start audio to retry.';
      void context?.close();
    };
    node.connect(nextContext.destination);
    context = nextContext;
    voice = node;
    keys.disabled = false;
    start.hidden = true;
    status.textContent = `Audio ready · ${context.sampleRate} Hz`;
  } catch (error) {
    if (nextContext) await nextContext.close();
    status.textContent = error instanceof Error ? error.message : 'Audio could not start.';
    start.disabled = false;
  }
});

for (const button of keys.querySelectorAll<HTMLButtonElement>('button')) {
  const key = Number(button.dataset.note);
  button.addEventListener('pointerdown', (event) => {
    if (event.button !== 0) return;
    button.setPointerCapture(event.pointerId);
    post({ on: true, key, source: event.pointerId });
  });
  for (const type of ['pointerup', 'pointercancel', 'lostpointercapture'] as const) {
    button.addEventListener(type, (event) => post({ on: false, key, source: event.pointerId }));
  }
  button.addEventListener('keydown', (event) => {
    if (event.key !== ' ' && event.key !== 'Enter') return;
    event.preventDefault();
    if (!event.repeat) post({ on: true, key, source: 0 });
  });
  button.addEventListener('keyup', (event) => {
    if (event.key !== ' ' && event.key !== 'Enter') return;
    event.preventDefault();
    post({ on: false, key, source: 0 });
  });
  button.addEventListener('blur', () => post({ on: false, key, source: 0 }));
}

window.addEventListener('blur', () => post({ release: true }));
