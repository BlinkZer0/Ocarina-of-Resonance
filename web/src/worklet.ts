type AudioExports = {
  memory: WebAssembly.Memory;
  _initialize?: () => void;
  prepare(rate: number): void;
  note(on: number, key: number, source: number): void;
  release(): void;
  render(frames: number): number;
};

export type VoiceMessage = { key: number; source: number; on: boolean } | { release: true };

declare const sampleRate: number;
declare class AudioWorkletProcessor {
  readonly port: MessagePort;
}
declare function registerProcessor(name: string, processor: new (options: AudioWorkletNodeOptions) => AudioWorkletProcessor): void;

class OcarinaVoice extends AudioWorkletProcessor {
  private readonly engine: AudioExports;
  private readonly memory: Float32Array;
  private peak = 0;
  private meterFrames = 0;

  constructor(options: AudioWorkletNodeOptions) {
    super();
    const module = options.processorOptions.module as WebAssembly.Module;
    this.engine = new WebAssembly.Instance(module).exports as AudioExports;
    this.engine._initialize?.();
    this.engine.prepare(sampleRate);
    this.memory = new Float32Array(this.engine.memory.buffer);
    this.port.onmessage = ({ data }: MessageEvent<VoiceMessage>) => {
      if ('release' in data) this.engine.release();
      else this.engine.note(Number(data.on), data.key, data.source);
    };
  }

  process(_inputs: Float32Array[][], outputs: Float32Array[][]): boolean {
    const channels = outputs[0];
    const frames = channels?.[0]?.length ?? 0;
    for (let start = 0; start < frames; start += 256) {
      const count = Math.min(256, frames - start);
      const offset = this.engine.render(count) / Float32Array.BYTES_PER_ELEMENT;
      for (let frame = 0; frame < count; ++frame) {
        const sample = this.memory[offset + frame];
        this.peak = Math.max(this.peak, Math.abs(sample));
        for (const channel of channels) channel[start + frame] = sample;
      }
    }
    this.meterFrames += frames;
    if (this.meterFrames >= sampleRate / 15) {
      this.port.postMessage(this.peak);
      this.peak = 0;
      this.meterFrames = 0;
    }
    return true;
  }
}

registerProcessor('ocarina-voice', OcarinaVoice);
