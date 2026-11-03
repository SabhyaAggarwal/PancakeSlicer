import type { Config } from '../api/types';
import { effectiveConfig, getState, markStale, resolvePreset, setState, type State } from '../state/store';
import { undoable } from '../state/history';
import { findProfile, profileKey } from './identity';

const DEFAULT_COLOUR = '#00AE42';
const hex = (value: unknown): string | undefined => {
  const v = String(Array.isArray(value) ? value[0] ?? '' : value ?? '');
  return /^#[\da-f]{6}/i.test(v) ? v.slice(0, 7).toUpperCase() : undefined;
};
export const configColour = (config: Config) => hex(config.filament_colour) ?? hex(config.default_filament_colour);

export function profileSlotColour(s: State, slot: number): string {
  return (s.filamentSlots[slot] ?? s.filamentSlots[0]) ? configColour(effectiveConfig(s, 'filament', slot)) ?? DEFAULT_COLOUR : DEFAULT_COLOUR;
}
export function slotColourOverride(s: State, slot: number): string | undefined {
  if (s.mixes[slot]) return undefined;
  const own = hex(s.slotColours[slot]);
  return own && own !== profileSlotColour(s, slot) ? own : undefined;
}
export function slotColour(s: State, slot: number): string {
  return (s.mixes[slot] ? hex(s.slotColours[slot]) : slotColourOverride(s, slot)) ?? profileSlotColour(s, slot);
}

export function useProfileColour(slot: number) {
  undoable('Use profile colour', () => setState((st) => {
    const slotColours = [...st.slotColours]; slotColours[slot] = undefined;
    return { slotColours, job: markStale(st.job) };
  }));
}

const ownedSource = (s: State, slot: number) => {
  const p = findProfile(s, 'filament', s.filamentSlots[slot] ?? '');
  if (!p || p.archived) throw new Error('Choose a saved filament for this slot first.');
  return p;
};
const withColour = (config: Config, colour: string): Config =>
  ({ ...config, filament_colour: Array.isArray(config.filament_colour) ? [colour] : colour });

export async function changeFilamentColour(slot: number, colour: string): Promise<void> {
  const value = hex(colour); if (!value) throw new Error('Choose a colour.');
  const s = getState(), source = ownedSource(s, slot), key = profileKey(source);
  const [{ saveProfile }, { commitProfileRecords }] = await Promise.all([import('./model'), import('./transactions')]);
  const record = { ...saveProfile(s, 'filament', source.name, key, withColour(resolvePreset(s, 'filament', key).config, value)), id: key };
  await commitProfileRecords([record], { apply: (st) => ({
    slotColours: st.slotColours.map((c, i) => st.filamentSlots[i] === key && hex(c) === value ? undefined : c) }) });
}

const COLOUR_NAMES: [number, string][] = [[15, 'Red'], [40, 'Orange'], [65, 'Yellow'], [165, 'Green'], [195, 'Cyan'], [255, 'Blue'], [290, 'Purple'], [335, 'Pink'], [360, 'Red']];
export function colourName(colour: string): string {
  const v = hex(colour) ?? DEFAULT_COLOUR, [r, g, b] = [1, 3, 5].map((i) => parseInt(v.slice(i, i + 2), 16) / 255);
  const max = Math.max(r, g, b), min = Math.min(r, g, b), l = (max + min) / 2, d = max - min;
  const sat = d === 0 ? 0 : d / (1 - Math.abs(2 * l - 1));
  if (l < 0.12) return 'Black';
  if (l > 0.92) return 'White';
  if (sat < 0.15) return l > 0.6 ? 'Silver' : 'Gray';
  const h = d === 0 ? 0 : max === r ? 60 * (((g - b) / d) % 6) : max === g ? 60 * ((b - r) / d + 2) : 60 * ((r - g) / d + 4);
  const hue = (h + 360) % 360, name = COLOUR_NAMES.find(([limit]) => hue < limit)![1];
  return (name === 'Orange' || name === 'Red') && l < 0.35 ? 'Brown' : name;
}

export async function saveFilamentAsColour(slot: number, colour: string): Promise<string> {
  const value = hex(colour); if (!value) throw new Error('Choose a colour.');
  const s = getState(), source = ownedSource(s, slot), key = profileKey(source);
  const stem = `${source.name.replace(new RegExp(` ${colourName(configColour(resolvePreset(s, 'filament', key).config) ?? '')}$`), '')} ${colourName(value)}`;
  const [{ saveProfile }, { profileCreation }] = await Promise.all([import('./model'), import('./transactions')]);
  const id = await profileCreation((st) => {
    const taken = (name: string) => st.userPresets.some((p) => p.type === 'filament' && p.name === name && !p.archived);
    let name = stem; for (let n = 2; taken(name); n++) name = `${stem} ${n}`;
    return { ...saveProfile(st, 'filament', name, key, withColour(resolvePreset(st, 'filament', key).config, value)), id: `profile:${crypto.randomUUID()}`, revision: 0 };
  }).commit();
  (await import('../components/PresetCombo')).requestPreset('filament', slot, id);
  setState((st) => { const slotColours = [...st.slotColours]; slotColours[slot] = undefined; return { slotColours }; });
  return id;
}
