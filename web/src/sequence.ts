import { barTicks, maxBars, maxFileBytes, maxNotes, ppq, readMidi, writeMidi, type RollNote } from './midi-file.js';

const get = <T extends HTMLElement>(id: string) => document.getElementById(id)! as T;
const clamp = (value: number, low: number, high: number) => Math.max(low, Math.min(value, high));
const row = 24, beat = 64;
const names = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];
const name = (key: number) => names[key % 12] + (Math.floor(key / 12) - 1);
const storage = 'ocarina-piano-roll-v1';
type Edit = { pointer: number; note: RollNote; start: number; end: number; offset: number; mode: 'draw' | 'move' | 'resize'; element: HTMLElement };
type Bridge = {
  platform: 'web' | 'desktop' | 'android';
  send: (packet: number[]) => void;
  startAudio: () => Promise<boolean>;
  fullscreen: (target: HTMLElement) => void;
  importFile: () => void;
  exportFile: (data: string) => void;
};

export class PianoRoll {
  private readonly panel = get<HTMLDetailsElement>('sequencer');
  private readonly grid = get('roll-grid');
  private readonly ruler = get('roll-ruler');
  private readonly viewport = get('roll-scroll');
  private readonly content = get('roll-content');
  private readonly cursor = get('roll-cursor');
  private readonly bpm = get<HTMLInputElement>('roll-tempo');
  private readonly bars = get<HTMLInputElement>('roll-bars');
  private readonly snap = get<HTMLSelectElement>('roll-snap');
  private readonly tool = get<HTMLSelectElement>('roll-tool');
  private readonly loop = get<HTMLInputElement>('roll-loop');
  private readonly sync = get<HTMLInputElement>('roll-sync');
  private readonly velocity = get<HTMLInputElement>('roll-velocity');
  private readonly notes: RollNote[] = [];
  private selected?: RollNote;
  private edit?: Edit;
  private tick = 0;
  private transport = 0;
  private command = 0;
  private desired = 0;
  private positioned = false;

  constructor(private readonly bridge: Bridge) {
    get('roll-sync-label').hidden = bridge.platform !== 'desktop';
    for (let key = 127; key >= 0; --key) {
      const label = document.createElement('span');
      label.textContent = name(key);
      if (names[key % 12].includes('♯')) label.classList.add('accidental');
      get('roll-pitches').append(label);
    }
    this.panel.addEventListener('toggle', () => {
      if (!this.panel.open && this.panel.classList.contains('performance-full')) bridge.fullscreen(this.panel);
      this.center();
    });
    get('roll-full').addEventListener('click', () => { this.panel.open = true; bridge.fullscreen(this.panel); this.center(); });
    get('roll-play').addEventListener('click', async () => {
      if (this.desired === 1 || this.transport === 2) return;
      if (await bridge.startAudio()) this.moveTransport(this.tick >= this.length ? 0 : this.tick, 1);
    });
    get('roll-stop').addEventListener('click', () => this.moveTransport(this.tick, 0));
    get('roll-rewind').addEventListener('click', () => this.moveTransport(0, this.transport === 1 ? 1 : 0));
    get('roll-clear').addEventListener('click', () => {
      this.notes.length = 0;
      this.select(undefined);
      this.drawNotes();
      this.moveTransport(0, 0);
    });
    this.bpm.addEventListener('change', () => {
      this.bpm.value = String(clamp(this.bpm.valueAsNumber || 120, 40, 250));
      this.publish();
    });
    this.bars.addEventListener('change', () => {
      const min = Math.max(1, Math.ceil(Math.max(0, ...this.notes.map(note => note.end)) / barTicks));
      if (this.bars.valueAsNumber < min) this.notice('Remove notes beyond the new length before shortening the roll.');
      this.bars.value = String(clamp(Math.round(this.bars.valueAsNumber || min), min, maxBars));
      this.drawRuler();
      this.publish();
    });
    this.snap.addEventListener('change', () => this.grid.style.setProperty('--step', this.toX(this.division) + 'px'));
    this.tool.addEventListener('change', () => { this.grid.dataset.tool = this.tool.value; });
    this.loop.addEventListener('change', () => this.publish());
    this.sync.addEventListener('change', () => this.publish());
    this.velocity.addEventListener('change', () => {
      this.velocity.value = String(clamp(Math.round(this.velocity.valueAsNumber || 1), 1, 127));
      if (this.selected) { this.selected.velocity = this.velocity.valueAsNumber; this.drawNotes(); this.publish(); }
    });
    this.ruler.addEventListener('pointerdown', event => {
      if (this.transport === 2) return;
      const x = event.clientX - this.grid.getBoundingClientRect().left;
      if (x >= 0) this.moveTransport(this.snapped(x), this.transport === 1 ? 1 : 0);
    });
    this.ruler.addEventListener('keydown', event => {
      if (this.transport === 2) return;
      if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return;
      event.preventDefault();
      this.moveTransport(this.tick + (event.key === 'ArrowLeft' ? -this.division : this.division), this.transport === 1 ? 1 : 0);
    });
    this.grid.addEventListener('pointerdown', event => this.pointerDown(event));
    this.grid.addEventListener('pointermove', event => this.pointerMove(event));
    for (const type of ['pointerup', 'pointercancel', 'lostpointercapture'] as const) {
      this.grid.addEventListener(type, event => {
        if (event.pointerId !== this.edit?.pointer) return;
        this.edit = undefined;
        this.publish();
      });
    }
    this.grid.addEventListener('contextmenu', event => event.preventDefault());
    this.grid.addEventListener('keydown', event => this.editKey(event));
    const input = get<HTMLInputElement>('roll-file');
    get('roll-import').addEventListener('click', () => bridge.platform === 'web' ? input.click() : bridge.importFile());
    input.addEventListener('change', async () => {
      const file = input.files?.[0];
      input.value = '';
      if (!file) return;
      if (file.size > maxFileBytes) { this.notice('MIDI files must be 128 KB or smaller.'); return; }
      try { this.loadMidi(new Uint8Array(await file.arrayBuffer())); }
      catch { this.notice('Could not open MIDI file.'); }
    });
    get('roll-export').addEventListener('click', () => {
      if (!this.notes.length) { this.notice('Draw or import some notes first.'); return; }
      const bytes = writeMidi(this.notes, this.bpm.valueAsNumber, this.bars.valueAsNumber);
      if (bridge.platform !== 'web') { bridge.exportFile(btoa(String.fromCharCode(...bytes))); return; }
      const url = URL.createObjectURL(new Blob([new Uint8Array(bytes)], { type: 'audio/midi' }));
      const link = document.createElement('a');
      link.href = url;
      link.download = 'Ocarina sequence.mid';
      link.click();
      setTimeout(() => URL.revokeObjectURL(url), 10000);
      this.notice('MIDI exported.');
    });
    if (bridge.platform === 'web') {
      try { this.restore(JSON.parse(localStorage.getItem(storage) || 'null')); } catch { /* start with a blank roll */ }
    }
    this.drawRuler();
    this.drawNotes();
  }

  private get length(): number { return this.bars.valueAsNumber * barTicks; }
  private get division(): number { return Number(this.snap.value); }
  private toX(tick: number): number { return tick * beat / ppq; }
  private snapped(x: number): number { return clamp(Math.floor(x / this.toX(this.division)) * this.division, 0, this.length); }

  private center(): void {
    if (this.positioned || !this.panel.open || this.viewport.clientHeight === 0) return;
    const highest = Math.max(76, ...this.notes.map(note => note.key));
    this.viewport.scrollTop = Math.max(0, 125 - highest) * row;
    this.positioned = true;
  }

  private drawRuler(): void {
    this.content.style.setProperty('--width', this.toX(this.length) + 'px');
    this.grid.style.setProperty('--step', this.toX(this.division) + 'px');
    this.ruler.replaceChildren();
    for (let bar = 0; bar < this.bars.valueAsNumber; ++bar) {
      const label = document.createElement('span');
      label.textContent = String(bar + 1);
      this.ruler.append(label);
    }
  }

  private drawNotes(): void {
    this.grid.replaceChildren(this.cursor);
    for (const note of this.notes) this.grid.append(this.makeNote(note));
  }

  private makeNote(note: RollNote): HTMLElement {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'roll-note';
    const grip = document.createElement('span');
    grip.className = 'roll-resize';
    button.append(grip);
    this.paintNote(button, note);
    return button;
  }

  private paintNote(element: HTMLElement, note: RollNote): void {
    element.dataset.index = String(this.notes.indexOf(note));
    element.style.left = this.toX(note.start) + 'px';
    element.style.top = ((127 - note.key) * row + 2) + 'px';
    element.style.width = Math.max(6, this.toX(note.end - note.start)) + 'px';
    element.style.setProperty('--velocity', String(.35 + .65 * note.velocity / 127));
    element.setAttribute('aria-pressed', String(this.selected === note));
    element.setAttribute('aria-label', name(note.key) + ', beat ' + (1 + note.start / ppq).toFixed(2)
      + ', velocity ' + note.velocity);
  }

  private select(note: RollNote | undefined): void {
    this.selected = note;
    if (note) this.velocity.value = String(note.velocity);
    for (const element of this.grid.querySelectorAll<HTMLElement>('.roll-note'))
      element.setAttribute('aria-pressed', String(this.notes[Number(element.dataset.index)] === note));
  }

  private pointerDown(event: PointerEvent): void {
    if (this.edit || this.tool.value === 'scroll' || (event.button !== 0 && event.button !== 2)) return;
    event.preventDefault();
    const target = event.target as HTMLElement;
    let element = target.closest<HTMLElement>('.roll-note') ?? undefined;
    let note = element ? this.notes[Number(element.dataset.index)] : undefined;
    if (event.button === 2 || this.tool.value === 'erase') {
      if (note) this.erase(note);
      return;
    }
    const box = this.grid.getBoundingClientRect();
    const start = this.snapped(event.clientX - box.left);
    const mode = note ? target.classList.contains('roll-resize') ? 'resize' : 'move' : 'draw';
    if (!note) {
      if (this.notes.length >= maxNotes) { this.notice('A piano roll holds up to 512 notes.'); return; }
      if (start >= this.length) return;
      note = { start, end: Math.min(this.length, start + this.division),
        key: clamp(127 - Math.floor((event.clientY - box.top) / row), 0, 127), velocity: this.velocity.valueAsNumber };
      this.notes.push(note);
      element = this.makeNote(note);
      this.grid.append(element);
    }
    this.select(note);
    this.grid.focus({ preventScroll: true });
    this.grid.setPointerCapture(event.pointerId);
    this.edit = { pointer: event.pointerId, note, start: note.start, end: note.end,
      offset: event.clientX - box.left - this.toX(note.start), mode, element: element! };
  }

  private pointerMove(event: PointerEvent): void {
    const edit = this.edit;
    if (!edit || event.pointerId !== edit.pointer) return;
    const { note } = edit;
    const box = this.grid.getBoundingClientRect();
    const x = event.clientX - box.left;
    if (edit.mode === 'move') {
      const duration = edit.end - edit.start;
      note.start = Math.min(this.length - duration, this.snapped(x - edit.offset + this.toX(this.division) / 2));
      note.end = note.start + duration;
      note.key = clamp(127 - Math.floor((event.clientY - box.top) / row), 0, 127);
    } else {
      note.end = Math.min(this.length, Math.max(note.start + Math.min(this.division, this.length - note.start),
        this.snapped(x) + (edit.mode === 'draw' ? this.division : 0)));
    }
    this.paintNote(edit.element, note);
  }

  private editKey(event: KeyboardEvent): void {
    const button = (event.target as HTMLElement).closest<HTMLElement>('.roll-note');
    const note = button ? this.notes[Number(button.dataset.index)] : this.selected;
    if (!note) return;
    if (event.key === 'Delete' || event.key === 'Backspace') { event.preventDefault(); this.erase(note); return; }
    if (event.key === 'Enter' && button) { this.select(note); return; }
    if (event.key === 'ArrowUp' || event.key === 'ArrowDown') {
      note.key = clamp(note.key + (event.key === 'ArrowUp' ? 1 : -1), 0, 127);
    } else if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') {
      const amount = (event.key === 'ArrowRight' ? 1 : -1) * this.division;
      if (event.shiftKey) note.end = clamp(note.end + amount, note.start + 1, this.length);
      else {
        const duration = note.end - note.start;
        note.start = clamp(note.start + amount, 0, this.length - duration);
        note.end = note.start + duration;
      }
    } else return;
    event.preventDefault();
    this.select(note);
    const element = this.grid.querySelector<HTMLElement>('[data-index="' + this.notes.indexOf(note) + '"]');
    if (element) this.paintNote(element, note);
    this.publish();
  }

  private erase(note: RollNote): void {
    this.notes.splice(this.notes.indexOf(note), 1);
    if (this.selected === note) this.select(undefined);
    this.drawNotes();
    this.publish();
  }

  private moveTransport(tick: number, play: number): void {
    this.tick = Math.round(clamp(tick, 0, this.length));
    this.desired = play;
    this.command = this.command >= 2147483647 ? 1 : this.command + 1;
    this.publish();
    this.drawCursor();
  }

  private drawCursor(): void {
    this.cursor.style.left = this.toX(this.tick) + 'px';
    get('roll-position').textContent = (1 + Math.floor(this.tick / barTicks)) + '.'
      + (1 + Math.floor(this.tick / ppq) % 4);
  }

  snapshot(): number[] {
    return [this.length, Math.round(this.bpm.valueAsNumber * 100), Number(this.loop.checked), Number(this.sync.checked),
      this.command, this.desired, clamp(this.tick, 0, this.length), ...this.notes.flatMap(note => [note.key, note.start, note.end, note.velocity])];
  }

  private publish(): void {
    const packet = this.snapshot();
    if (this.bridge.platform === 'web') {
      try {
        const saved = packet.slice();
        saved[4] = saved[5] = saved[6] = 0;
        localStorage.setItem(storage, JSON.stringify(saved));
      } catch { /* the current roll remains playable */ }
    }
    this.bridge.send(packet);
  }

  restore(packet: unknown): void {
    if (!Array.isArray(packet) || packet.length < 7 || packet.length > 7 + maxNotes * 4 || (packet.length - 7) % 4
      || !packet.every(Number.isInteger) || packet[0] % barTicks || packet[0] < barTicks || packet[0] > maxBars * barTicks
      || packet[1] < 4000 || packet[1] > 25000) return;
    const notes: RollNote[] = [];
    for (let index = 7; index < packet.length; index += 4) {
      const [key, start, end, velocity] = packet.slice(index, index + 4);
      if (key < 0 || key > 127 || start < 0 || end <= start || end > packet[0] || velocity < 1 || velocity > 127) return;
      notes.push({ key, start, end, velocity });
    }
    this.notes.splice(0, this.notes.length, ...notes);
    this.select(undefined);
    this.bars.value = String(packet[0] / barTicks);
    this.bpm.value = String(packet[1] / 100);
    this.loop.checked = packet[2] !== 0;
    this.sync.checked = packet[3] !== 0;
    this.tick = this.desired = 0;
    this.drawRuler();
    this.drawNotes();
    this.drawCursor();
    this.positioned = false;
    this.center();
  }

  telemetry(tick: number, transport: number): void {
    this.tick = clamp(tick, 0, this.length);
    if (this.transport !== transport) {
      this.transport = transport;
      if (transport === 0) this.desired = 0;
      get('roll-state').textContent = transport === 2 ? 'Following DAW' : transport === 1 ? 'Playing' : 'Stopped';
      for (const id of ['roll-play', 'roll-stop', 'roll-rewind']) get<HTMLButtonElement>(id).disabled = transport === 2;
    }
    this.drawCursor();
  }

  pauseLocal(): void {
    if (this.bridge.platform !== 'desktop' && (this.transport === 1 || this.desired === 1)) this.moveTransport(this.tick, 0);
  }

  notice(text: string): void { get('roll-notice').textContent = text; }

  importBase64(data: string): void {
    try {
      if (data.length > Math.ceil(maxFileBytes / 3) * 4) throw new Error('MIDI files must be 128 KB or smaller.');
      this.loadMidi(Uint8Array.from(atob(data), byte => byte.charCodeAt(0)));
    } catch (error) { this.notice((error as Error).message); }
  }

  private loadMidi(bytes: Uint8Array): void {
    try {
      const { notes, bpm, bars } = readMidi(bytes);
      this.notes.splice(0, this.notes.length, ...notes);
      this.select(undefined);
      this.bars.value = String(bars);
      this.bpm.value = String(bpm);
      this.drawRuler();
      this.drawNotes();
      this.moveTransport(0, 0);
      this.notice('Imported ' + notes.length + ' MIDI notes.');
      this.positioned = false;
      this.center();
    } catch (error) { this.notice((error as Error).message); }
  }
}
