export type Control = { index: number; id: string; name: string; unit: string; min: number; max: number; initial: number };
export type Effect = { id: string; name: string; controls: string[]; choices: Record<string, string[]> };
export type Preset = {
  schemaVersion: 1; id: string; name: string; category: string; description: string;
  parameters: Record<string, number>;
  effects: { id: string; enabled: boolean; parameters: Record<string, number> }[];
  performance: { model: string; voiceMode: 'mono' | 'poly'; voiceLimit: number; gainCompensationDb: number; tempoBpm: number };
};

const models = ['ceramicAuthentic', 'retroFantasy', 'crystalResonator', 'electronicHybrid'];
const record = (value: unknown, label: string): Record<string, unknown> => {
  if (typeof value !== 'object' || !value || Array.isArray(value)) throw new Error(`Invalid ${label}.`);
  return value as Record<string, unknown>;
};

function label(value: unknown, field: string, limit: number, required = true): string {
  if (typeof value !== 'string' || value.length > limit || (required && !value.trim())) throw new Error(`Invalid ${field}.`);
  return value.trim();
}

export class PresetFormat {
  readonly controls: Map<string, Control>;
  private readonly synth: Control[];

  constructor(controls: Control[], readonly effects: Effect[]) {
    this.controls = new Map(controls.map(control => [control.id, control]));
    const reserved = new Set(['model', 'voiceMode', 'voiceLimit', 'patchGain', 'tempo',
      ...effects.flatMap(effect => [effect.id + 'Enabled', ...effect.controls])]);
    this.synth = controls.filter(control => !reserved.has(control.id));
  }

  private numeric(id: string, value: unknown, integer = false): number {
    const control = this.controls.get(id);
    if (!control || typeof value !== 'number' || !Number.isFinite(value) || value < control.min || value > control.max
        || (integer && !Number.isInteger(value))) throw new Error(`Invalid ${id}.`);
    return value;
  }

  read(input: unknown): Preset {
    const raw = record(input, 'preset');
    if (raw.schemaVersion !== 1) throw new Error('This preset schema version is unsupported.');
    const id = label(raw.id, 'preset ID', 96);
    if (!/^[a-z0-9][a-z0-9-]*$/.test(id)) throw new Error('Invalid preset ID.');
    const synth = record(raw.parameters, 'synthesis parameters');
    const parameters: Record<string, number> = Object.fromEntries(this.synth.map(control => [control.id, control.initial]));
    for (const [key, value] of Object.entries(synth)) {
      if (!Object.hasOwn(parameters, key)) throw new Error(`Unknown synthesis parameter ${key}.`);
      parameters[key] = this.numeric(key, value);
    }

    const performance = record(raw.performance, 'performance settings');
    if (typeof performance.model !== 'string' || !models.includes(performance.model)) throw new Error('Unknown sound model.');
    if (performance.voiceMode !== 'mono' && performance.voiceMode !== 'poly') throw new Error('Invalid voice mode.');
    if (!Array.isArray(raw.effects) || raw.effects.length > this.effects.length) throw new Error('Invalid effects list.');
    const listed = new Map<string, Record<string, unknown>>();
    for (const value of raw.effects) {
      const effect = record(value, 'effect');
      if (typeof effect.id !== 'string' || !this.effects.some(spec => spec.id === effect.id) || listed.has(effect.id)
          || typeof effect.enabled !== 'boolean') throw new Error('Unknown or repeated effect.');
      listed.set(effect.id, effect);
    }
    const effects = this.effects.map(spec => {
      const effect = listed.get(spec.id);
      const overrides = effect ? record(effect.parameters, spec.name) : {};
      const parameters: Record<string, number> = Object.fromEntries(spec.controls.map(key => [key, this.controls.get(key)!.initial]));
      for (const [key, value] of Object.entries(overrides)) {
        if (!Object.hasOwn(parameters, key)) throw new Error(`Unknown ${spec.name} parameter ${key}.`);
        parameters[key] = this.numeric(key, value, Object.hasOwn(spec.choices, key) || key === 'crusherBits');
      }
      return { id: spec.id, enabled: effect?.enabled === true, parameters };
    });
    return {
      schemaVersion: 1, id, name: label(raw.name, 'preset name', 80), category: label(raw.category, 'category', 48),
      description: label(raw.description, 'description', 320, false), parameters, effects,
      performance: { model: performance.model, voiceMode: performance.voiceMode,
        voiceLimit: this.numeric('voiceLimit', performance.voiceLimit, true),
        gainCompensationDb: this.numeric('patchGain', performance.gainCompensationDb),
        tempoBpm: this.numeric('tempo', performance.tempoBpm === undefined ? 120 : performance.tempoBpm) },
    };
  }

  values(preset: Preset): number[] {
    const values = Array.from(this.controls.values(), control => control.initial);
    const set = (id: string, value: number) => { values[this.controls.get(id)!.index] = value; };
    for (const [id, value] of Object.entries(preset.parameters)) set(id, value);
    for (const effect of preset.effects) {
      set(effect.id + 'Enabled', Number(effect.enabled));
      for (const [id, value] of Object.entries(effect.parameters)) set(id, value);
    }
    set('model', models.indexOf(preset.performance.model));
    set('voiceMode', Number(preset.performance.voiceMode === 'poly'));
    set('voiceLimit', preset.performance.voiceLimit);
    set('patchGain', preset.performance.gainCompensationDb);
    set('tempo', preset.performance.tempoBpm);
    return values;
  }

  capture(values: number[], metadata: Pick<Preset, 'id' | 'name' | 'category' | 'description'>): Preset {
    const get = (id: string) => values[this.controls.get(id)!.index];
    return this.read({
      ...metadata, schemaVersion: 1,
      parameters: Object.fromEntries(this.synth.map(control => [control.id, get(control.id)])),
      effects: this.effects.map(effect => ({ id: effect.id, enabled: get(effect.id + 'Enabled') >= .5,
        parameters: Object.fromEntries(effect.controls.map(id => [id, get(id)])) })),
      performance: { model: models[get('model')], voiceMode: get('voiceMode') >= .5 ? 'poly' : 'mono',
        voiceLimit: get('voiceLimit'), gainCompensationDb: get('patchGain'), tempoBpm: get('tempo') },
    });
  }
}
