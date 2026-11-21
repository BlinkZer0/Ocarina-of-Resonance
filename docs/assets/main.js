const start = document.querySelector('#start');
const status = document.querySelector('#status');
const keys = document.querySelector('#keys');
const meter = document.querySelector('#meter');
const model = document.querySelector('#model');
const polyphony = document.querySelector('#polyphony');
let context;
let voice;
function post(message) {
    voice?.port.postMessage(message);
}
function setPerformance() {
    post({ model: Number(model.value), voices: Number(polyphony.value) });
}
function audioAsset(name) {
    const moduleUrl = new URL(import.meta.url);
    const asset = new URL(name, moduleUrl);
    asset.search = moduleUrl.search;
    return asset;
}
for (let count = 1; count <= 16; ++count) {
    polyphony.add(new Option(`${count} voices`, String(count), count === 16, count === 16));
}
model.addEventListener('change', setPerformance);
polyphony.addEventListener('change', setPerformance);
start.addEventListener('click', async () => {
    start.disabled = true;
    status.textContent = 'Starting audio…';
    let nextContext;
    try {
        if (!window.AudioContext || !window.isSecureContext || !window.WebAssembly) {
            throw new Error('Use a current browser over HTTPS or localhost to play.');
        }
        nextContext = new AudioContext({ latencyHint: 'interactive' });
        const resume = nextContext.resume();
        // Start resuming in the gesture handler before fetching the worklet or Wasm.
        await resume;
        const [response] = await Promise.all([
            fetch(audioAsset('./ocarina.wasm')),
            nextContext.audioWorklet.addModule(audioAsset('./worklet.js')),
        ]);
        if (!response.ok)
            throw new Error(`Audio module unavailable (${response.status}).`);
        const module = await WebAssembly.compile(await response.arrayBuffer());
        const node = new AudioWorkletNode(nextContext, 'ocarina-voice', {
            numberOfInputs: 0, outputChannelCount: [2], processorOptions: { module },
        });
        node.port.onmessage = ({ data }) => { meter.value = data; };
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
        setPerformance();
        keys.disabled = false;
        start.hidden = true;
        status.textContent = `Audio ready · ${context.sampleRate} Hz`;
    }
    catch (error) {
        if (nextContext)
            await nextContext.close();
        status.textContent = error instanceof Error ? error.message : 'Audio could not start.';
        start.disabled = false;
    }
});
for (const button of keys.querySelectorAll('button')) {
    const key = Number(button.dataset.note);
    button.addEventListener('pointerdown', (event) => {
        if (event.button !== 0)
            return;
        button.setPointerCapture(event.pointerId);
        post({ on: true, key, source: event.pointerId });
    });
    for (const type of ['pointerup', 'pointercancel', 'lostpointercapture']) {
        button.addEventListener(type, (event) => post({ on: false, key, source: event.pointerId }));
    }
    button.addEventListener('keydown', (event) => {
        if (event.key !== ' ' && event.key !== 'Enter')
            return;
        event.preventDefault();
        if (!event.repeat)
            post({ on: true, key, source: 0 });
    });
    button.addEventListener('keyup', (event) => {
        if (event.key !== ' ' && event.key !== 'Enter')
            return;
        event.preventDefault();
        post({ on: false, key, source: 0 });
    });
    button.addEventListener('blur', () => post({ on: false, key, source: 0 }));
}
window.addEventListener('blur', () => post({ release: true }));
export {};
