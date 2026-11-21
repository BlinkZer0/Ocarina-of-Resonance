export type RollNote = { key: number; start: number; end: number; velocity: number };
export const ppq = 960, barTicks = 4 * ppq, maxBars = 64, maxNotes = 512, maxFileBytes = 128 * 1024;

export function readMidi(bytes: Uint8Array): { notes: RollNote[]; bpm: number; bars: number } {
  if (bytes.length < 14 || bytes.length > maxFileBytes) throw new Error('MIDI files must be 128 KB or smaller.');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const tag = (at: number) => view.getUint32(at);
  const header = view.getUint32(4);
  const format = view.getUint16(8), tracks = view.getUint16(10), division = view.getUint16(12);
  if (tag(0) !== 0x4d546864 || header < 6 || header + 8 > bytes.length || format > 1
      || tracks < 1 || (format === 0 && tracks !== 1) || division === 0 || division & 0x8000)
    throw new Error('Use a format 0 or 1 MIDI file with quarter-note timing.');
  let at = header + 8, total = 0, bpm = 120, tempoTick = Infinity;
  const notes: RollNote[] = [];
  for (let track = 0; track < tracks; ++track) {
    if (at + 8 > bytes.length || tag(at) !== 0x4d54726b) throw new Error('Invalid MIDI track.');
    const end = at + 8 + view.getUint32(at + 4);
    if (end > bytes.length) throw new Error('Truncated MIDI track.');
    at += 8;
    const byte = () => {
      if (at >= end) throw new Error('Truncated MIDI event.');
      return bytes[at++];
    };
    const variable = () => {
      let value = 0;
      for (let i = 0; i < 4; ++i) {
        const n = byte();
        value = (value << 7) | (n & 127);
        if (n < 128) return value;
      }
      throw new Error('Invalid MIDI timing value.');
    };
    const skip = (size: number) => {
      at += size;
      if (at > end) throw new Error('Truncated MIDI event.');
    };
    let elapsed = 0, status = 0, tick = 0;
    const held = new Map<number, RollNote[]>();
    const finish = (note: RollNote) => {
      if (notes.length >= maxNotes) throw new Error('A piano roll holds up to 512 notes.');
      note.end = Math.max(note.start + 1, tick);
      notes.push(note);
    };
    while (at < end) {
      elapsed += variable();
      tick = Math.round(elapsed * ppq / division);
      if (tick > maxBars * barTicks) throw new Error('A piano roll holds up to 64 bars.');
      const message = bytes[at] >= 128 ? byte() : status;
      if (message === 0xff) {
        status = 0;
        const type = byte(), size = variable();
        if (type === 0x51 && size === 3 && tick < tempoTick) {
          const micros = (byte() << 16) | (byte() << 8) | byte();
          if (micros > 0) {
            bpm = Math.round(Math.max(40, Math.min(250, 60e6 / micros)) * 100) / 100;
            tempoTick = tick;
          }
        } else skip(size);
        if (type === 0x2f) break;
      } else if (message === 0xf0 || message === 0xf7) {
        status = 0;
        skip(variable());
      } else {
        if (message < 0x80 || message >= 0xf0) throw new Error('Invalid MIDI message.');
        status = message;
        const kind = status & 0xf0, key = byte();
        const velocity = kind === 0xc0 || kind === 0xd0 ? 0 : byte();
        if (key > 127 || velocity > 127) throw new Error('Invalid MIDI data.');
        if (kind !== 0x80 && kind !== 0x90) continue;
        const id = (status & 15) * 128 + key;
        const pending = held.get(id) ?? [];
        if (kind === 0x90 && velocity > 0) {
          pending.push({ key, velocity, start: tick, end: 0 });
          held.set(id, pending);
        } else if (pending.length) {
          finish(pending.shift()!);
          if (!pending.length) held.delete(id);
        }
      }
    }
    for (const pending of held.values()) for (const note of pending) finish(note);
    total = Math.max(total, tick);
    at = end;
  }
  if (!notes.length) throw new Error('The MIDI file has no notes.');
  total = Math.max(total, ...notes.map(note => note.end));
  if (total > maxBars * barTicks) throw new Error('A piano roll holds up to 64 bars.');
  return { notes, bpm, bars: Math.max(1, Math.ceil(total / barTicks)) };
}

export function writeMidi(notes: RollNote[], bpm: number, bars: number): Uint8Array {
  const variable = (number: number) => {
    const bytes = [number & 127];
    while ((number >>>= 7) !== 0) bytes.unshift((number & 127) | 128);
    return bytes;
  };
  const micros = Math.round(60e6 / bpm);
  const track = [0, 255, 81, 3, micros >> 16, (micros >> 8) & 255, micros & 255];
  const edges = notes.flatMap(note => [
    { tick: note.start, key: note.key, velocity: note.velocity },
    { tick: note.end, key: note.key, velocity: 0 },
  ]).sort((a, b) => a.tick - b.tick || a.velocity - b.velocity || a.key - b.key);
  let cursor = 0;
  for (const edge of edges) {
    track.push(...variable(edge.tick - cursor), edge.velocity ? 0x90 : 0x80, edge.key, edge.velocity);
    cursor = edge.tick;
  }
  track.push(...variable(bars * barTicks - cursor), 255, 47, 0);
  const length = track.length;
  return Uint8Array.of(77, 84, 104, 100, 0, 0, 0, 6, 0, 0, 0, 1, ppq >> 8, ppq & 255,
    77, 84, 114, 107, length >>> 24, length >>> 16 & 255, length >>> 8 & 255, length & 255, ...track);
}
