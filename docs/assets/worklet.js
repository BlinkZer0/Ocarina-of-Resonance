class OcarinaVoice extends AudioWorkletProcessor {
    engine;
    memory;
    sequence;
    peak = 0;
    meterFrames = 0;
    constructor(options) {
        super();
        const module = options.processorOptions.module;
        this.engine = new WebAssembly.Instance(module).exports;
        this.engine._initialize?.();
        this.engine.prepare(sampleRate);
        this.memory = new Float32Array(this.engine.memory.buffer);
        this.sequence = new Int32Array(this.engine.memory.buffer, this.engine.sequenceData(), 7 + 512 * 4);
        this.port.onmessage = ({ data }) => {
            if ('release' in data)
                this.engine.releaseSource(data.release);
            else if ('patch' in data)
                data.patch.forEach((value, index) => this.engine.parameter(index, value));
            else if ('index' in data)
                this.engine.parameter(data.index, data.value);
            else if ('midi' in data)
                this.engine.midi(...data.midi);
            else if ('sequence' in data && data.sequence.length <= this.sequence.length) {
                this.sequence.set(data.sequence);
                this.engine.commitSequence(data.sequence.length);
            }
            else if ('key' in data)
                this.engine.keyNote(Number(data.on), data.key, data.source, data.velocity);
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
            this.port.postMessage({ peak: this.peak, voices: this.engine.activeVoices(), note: this.engine.latestNote(),
                tick: this.engine.sequencePosition(), transport: this.engine.sequenceActivity(),
                held: [0, 1, 2, 3].map(word => this.engine.heldNotes(word)) });
            this.peak = 0;
            this.meterFrames = 0;
        }
        return true;
    }
}
registerProcessor('ocarina-voice', OcarinaVoice);
export {};
