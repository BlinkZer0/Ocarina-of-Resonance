type AudioExports = {
  memory: WebAssembly.Memory;
  _initialize?: () => void;
  prepare(rate: number): void;
  keyNote(on: number, key: number, source: number, velocity: number): void;
  releaseSource(source: number): void;
  midi(source: number, status: number, first: number, second: number): void;
  sequenceData(): number;
  commitSequence(size: number): void;
  sequencePosition(): number;
  sequenceActivity(): number;
  heldNotes(index: number): number;
  parameter(index: number, value: number): void;
  activeVoices(): number;
  latestNote(): number;
  render(frames: number): number;
};

export type VoiceMessage = { key: number; source: number; on: boolean; velocity: number } | { release: number }
  | { index: number; value: number } | { patch: number[] } | { sequence: number[] }
  | { midi: [number, number, number, number] };

declare const sampleRate: number;
declare class AudioWorkletProcessor {
  readonly port: MessagePort;
}
declare function registerProcessor(name: string, processor: new (options: AudioWorkletNodeOptions) => AudioWorkletProcessor): void;

class OcarinaVoice extends AudioWorkletProcessor {
  private readonly engine: AudioExports;
  private readonly memory: Float32Array;
  private readonly sequence: Int32Array;
  private peak = 0;
  private meterFrames = 0;

  constructor(options: AudioWorkletNodeOptions) {
    super();
    const module = options.processorOptions.module as WebAssembly.Module;
    this.engine = new WebAssembly.Instance(module).exports as AudioExports;
    this.engine._initialize?.();
    this.engine.prepare(sampleRate);
    this.memory = new Float32Array(this.engine.memory.buffer);
    this.sequence = new Int32Array(this.engine.memory.buffer, this.engine.sequenceData(), 7 + 512 * 4);
    this.port.onmessage = ({ data }: MessageEvent<VoiceMessage>) => {
      if ('release' in data) this.engine.releaseSource(data.release);
      else if ('patch' in data) data.patch.forEach((value, index) => this.engine.parameter(index, value));
      else if ('index' in data) this.engine.parameter(data.index, data.value);
      else if ('midi' in data) this.engine.midi(...data.midi);
      else if ('sequence' in data && data.sequence.length <= this.sequence.length) {
        this.sequence.set(data.sequence);
        this.engine.commitSequence(data.sequence.length);
      } else if ('key' in data) this.engine.keyNote(Number(data.on), data.key, data.source, data.velocity);
    };
  }

  process(_inputs: Float32Array[][], outputs: Float32Array[][]): boolean {
    const channels = outputs[0];
    const frames = channels?.[0]?.length ?? 0;
    for (let start = 0; start < frames; start += 256) {
      const count = Math.min(256, frames - start);
      const offset = this.engine.render(count) / Float32Array.BYTES_PER_ELEMENT;
      for (let frame = 0; frame < count; ++frame) {
        const left = this.memory[offset + frame];
        const right = this.memory[offset + 256 + frame];
        this.peak = Math.max(this.peak, Math.abs(left), Math.abs(right));
        if (channels.length === 1) channels[0][start + frame] = (left + right) * .5;
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
