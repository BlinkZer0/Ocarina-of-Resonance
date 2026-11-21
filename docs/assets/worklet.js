class OcarinaVoice extends AudioWorkletProcessor {
    engine;
    memory;
    peak = 0;
    meterFrames = 0;
    constructor(options) {
        super();
        const module = options.processorOptions.module;
        this.engine = new WebAssembly.Instance(module).exports;
        this.engine._initialize?.();
        this.engine.prepare(sampleRate);
        this.memory = new Float32Array(this.engine.memory.buffer);
        this.port.onmessage = ({ data }) => {
            if ('release' in data)
                this.engine.release();
            else if ('patch' in data)
                data.patch.forEach((value, index) => this.engine.parameter(index, value));
            else if ('index' in data)
                this.engine.parameter(data.index, data.value);
            else
                this.engine.note(Number(data.on), data.key, data.source);
        };
    }
    process(_inputs, outputs) {
        const channels = outputs[0];
        const frames = channels?.[0]?.length ?? 0;
        for (let start = 0; start < frames; start += 256) {
            const count = Math.min(256, frames - start);
            const offset = this.engine.render(count) / Float32Array.BYTES_PER_ELEMENT;
            for (let frame = 0; frame < count; ++frame) {
                const left = this.memory[offset + frame];
                const right = this.memory[offset + 256 + frame];
                this.peak = Math.max(this.peak, Math.abs(left), Math.abs(right));
                if (channels.length === 1)
                    channels[0][start + frame] = (left + right) * .5;
                else {
                    channels[0][start + frame] = left;
                    channels[1][start + frame] = right;
                }
            }
        }
        this.meterFrames += frames;
        if (this.meterFrames >= sampleRate / 15) {
            this.port.postMessage({ peak: this.peak, voices: this.engine.activeVoices(), note: this.engine.latestNote() });
            this.peak = 0;
            this.meterFrames = 0;
        }
        return true;
    }
}
registerProcessor('ocarina-voice', OcarinaVoice);
export {};
