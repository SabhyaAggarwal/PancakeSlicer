





import type { Config, ConfigValue, PresetType } from '../api/types';
import { switchPreset } from '../actions';
import { selectChanges } from '../state/changes';
import { getState, presetEdits, resolvePreset, setState, toast, type State } from '../state/store';
import type { UserPreset } from '../api/types';
import { clampHeight, DEFAULT_SCALES, scaleValues, type ScaleRow } from './auto-rules';
import { effectiveLayerHeight, effectiveProfileValues, printerOverrides } from './context';
import { createProfileFromBase } from './catalog';
import { findProfile, profileKey } from './identity';
import { saveProfile } from './model';
import { acknowledgeProjectProfileChanges, captureProjectProfileChanges } from './project-draft';
import { archiveProfiles, commitProfileRecords, profileCreation } from './transactions';
import { scalesOf } from './auto-rules';

const selectedKey = (s: State, type: PresetType, slot: number) => (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[slot]) ?? '';
const message = (e: unknown) => e instanceof Error ? e.message : String(e);


export function autoOf(source: unknown, scales: ScaleRow[]): { auto?: object } {
  const prior = (source as { auto?: { preferredHeight?: number } } | undefined)?.auto;
  return scales.length || prior?.preferredHeight ? { auto: { ...prior, schema: 3, scales } } : {};
}

export type ValueChoice = 'formula' | 'fix' | 'project' | 'printer';
export interface ValueSource { source: 'formula' | 'printer'; row?: ScaleRow; printer?: string }

export const scaleContext = (s: State) => {
  const nozzle = Number([effectiveProfileValues(s, 'machine', s.printer).nozzle_diameter].flat()[0]) || 0;
  return { nozzle, layerHeight: effectiveLayerHeight(s) };
};

export function valueSources(s: State, type: PresetType, slot: number): Record<string, ValueSource> {
  if (type !== 'process') return {};
  const p = findProfile(s, type, selectedKey(s, type, slot)), own = p?.overlay ?? {}, printer = findProfile(s, 'machine', s.printer)?.name ?? 'printer';
  const out: Record<string, ValueSource> = Object.fromEntries(scalesOf(p).map((row) => [row.key, { source: 'formula' as const, row }]));
  for (const k of Object.keys(printerOverrides(s))) if (!(k in own)) out[k] = { source: 'printer', printer };
  return out;
}
const round = (v: number, d = 4) => Math.round(v * 10 ** d) / 10 ** d;


export function solveFactor(row: ScaleRow, value: ConfigValue, ctx: { nozzle: number; layerHeight: number }): number | undefined {
  const v = Number([value].flat()[0]), h = clampHeight(ctx.layerHeight, ctx.nozzle);
  if (!Number.isFinite(v) || !(ctx.nozzle > 0) || !(h > 0)) return undefined;
  if (row.basis === 'nozzle') return round(v / ctx.nozzle);
  if (row.basis === 'layer') return round(v / h);
  const n = Math.round(v);
  return n < 1 ? undefined : round(Math.floor((n - 1) * h * 100 + 1e-6) / 100 + 0.01, 2);
}
export const formulaText = (row: ScaleRow) => row.basis === 'nozzle' ? `${row.factor} × nozzle` : row.basis === 'layer' ? `${row.factor} × layer height` : `${row.factor} mm ÷ layer height`;

export const defaultChoice = (v: ValueSource): ValueChoice => v.source === 'formula' ? 'formula' : 'printer';



function applyChoices(scales: ScaleRow[], deltas: Config, sources: Record<string, ValueSource>, choices: Record<string, ValueChoice>, ctx: { nozzle: number; layerHeight: number }) {
  const own: Config = {}, toPrinter: Config = {}, kept: Config = {};
  let rows = scales;
  for (const [k, v] of Object.entries(deltas)) {
    const src = sources[k], choice = src ? choices[k] ?? defaultChoice(src) : undefined;
    if (!src) own[k] = v;
    else if (choice === 'project') kept[k] = v;
    else if (src.source === 'printer') toPrinter[k] = v;
    else if (choice === 'fix') { rows = rows.filter((r) => r.key !== k); own[k] = v; }
    else { const factor = solveFactor(src.row!, v, ctx); if (factor === undefined) kept[k] = v; else rows = rows.map((r) => r.key === k ? { ...r, factor } : r); }
  }
  return { rows, own, toPrinter, kept };
}

function printerWith(s: State, values: Config): UserPreset | undefined {
  const printer = findProfile(s, 'machine', s.printer);
  if (!printer || !Object.keys(values).length) return undefined;
  const prior = (printer as { printOverrides?: Config }).printOverrides ?? {};
  return { ...saveProfile(s, 'machine', printer.name, profileKey(printer), resolvePreset(s, 'machine', profileKey(printer)).config), id: profileKey(printer),
    applicability: printer.applicability, printOverrides: { ...prior, ...values } } as UserPreset;
}

export function savesInPlace(s: State, type: PresetType, slot: number): boolean {
  const p = findProfile(s, type, selectedKey(s, type, slot));
  return !!p && p.kind !== 'master';
}


function changesOf(s: State, type: PresetType, slot: number) {
  return { edits: presetEdits(s, type, slot), project: (type === 'filament' ? s.projectPresets?.filaments[slot] : s.projectPresets?.[type]) ?? null };
}
function restoreChanges(type: PresetType, slot: number, key: string, c: ReturnType<typeof changesOf>) {
  setState((st) => ({
    ...(type === 'filament' ? { filamentEdits: { ...st.filamentEdits, [key]: c.edits } } : { dirty: { ...st.dirty, [type]: c.edits } }),
    ...(st.projectPresets && c.project ? { projectPresets: type === 'filament'
      ? { ...st.projectPresets, filaments: st.projectPresets.filaments.map((f, i) => i === slot ? c.project! : f) }
      : { ...st.projectPresets, [type]: c.project } } : {}),
  }));
}



export async function saveToProfile(type: PresetType, slot = 0, choices: Record<string, ValueChoice> = {}): Promise<void> {
  const s = getState();
  if (!s.api) throw new Error('Not connected. Your changes are kept.');
  const stage = captureProjectProfileChanges(type, slot), source = findProfile(s, type, stage.key), resolved = resolvePreset(s, type, stage.key);
  if (!source || !resolved.base || resolved.error) throw new Error(resolved.error ?? 'This profile is unavailable. Your changes are kept.');
  const before = changesOf(s, type, slot), prior = structuredClone(source), printerBefore = structuredClone(findProfile(s, 'machine', s.printer));
  const scales = scalesOf(source), { rows, own, toPrinter, kept } = applyChoices(scales, stage.deltas, valueSources(s, type, slot), choices, scaleContext(s));
  if (Object.keys(kept).length === Object.keys(stage.deltas).length) throw new Error('Every change stays in this project. Choose where to save one.');

  const values = { ...resolved.config, ...own };
  for (const r of rows) delete values[r.key];
  const saved = { ...values, ...stage.deltas };
  for (const k of Object.keys(kept)) delete saved[k];
  const record = { ...saveProfile(s, type, source.name, profileKey(source), values), id: profileKey(source), applicability: source.applicability,
    ...(type === 'process' ? autoOf(source, rows) : {}) } as UserPreset;
  for (const r of rows) { delete record.overlay?.[r.key]; delete record.overrides?.[r.key]; }
  if (type === 'process' && !autoOf(source, rows).auto) delete (record as { auto?: unknown }).auto;
  const printer = printerWith(s, toPrinter);
  await commitProfileRecords([record, ...(printer ? [printer] : [])], { apply: (st) => acknowledgeProjectProfileChanges(st, stage, saved) });

  toast(`Saved to ${source.name}${printer ? ` and ${printer.name}` : ''}`, { label: 'Undo', run: () => {
    const now = getState(), back = [{ ...prior, revision: findProfile(now, type, stage.key)?.revision }];
    if (printer && printerBefore) back.push({ ...printerBefore, revision: findProfile(now, 'machine', profileKey(printerBefore))?.revision });
    void commitProfileRecords(back).then(() => restoreChanges(type, slot, stage.key, before), (e) => toast(message(e)));
  } });
}


export const newProfileName = (s: State, type: PresetType, slot: number) => {
  const r = resolvePreset(s, type, selectedKey(s, type, slot)), name = r.source?.name ?? r.base?.name ?? '';
  return name.startsWith('My ') ? name : `My ${name}`;
};


export function newProfileNameError(s: State, type: PresetType, name: string): string | null {
  const n = name.trim();
  if (!n) return 'Enter a name';
  return s.userPresets.some((p) => p.type === type && !p.archived && p.name === n) ? `“${n}” is already taken` : null;
}



export type NewKind = 'same' | 'dynamic' | 'regular';


export async function saveAsNewProfile(type: PresetType, slot: number, name: string, kind: NewKind = 'same', choices: Record<string, ValueChoice> = {}): Promise<void> {
  const s = getState();
  if (!s.api) throw new Error('Not connected. Your changes are kept.');
  const error = newProfileNameError(s, type, name);
  if (error) throw new Error(error);
  const key = selectedKey(s, type, slot), resolved = resolvePreset(s, type, key);
  if (!resolved.base || resolved.error) throw new Error(resolved.error ?? 'This profile is unavailable. Your changes are kept.');
  const deltas = Object.fromEntries(selectChanges(s, type, slot).filter((c) => c.scope === 'project' && c.owner.plate === undefined).map((c) => [c.key, c.now]));
  const before = changesOf(s, type, slot), source = findProfile(s, type, key), ctx = scaleContext(s);

  const scales = scalesOf(source), dynamic = type === 'process' && kind === 'dynamic' && scales.length > 0;
  const chosen = dynamic ? applyChoices(scales, deltas, valueSources(s, type, slot), choices, ctx) : undefined;
  const frozen = type === 'process' && kind === 'regular' ? { ...scaleValues(scales, ctx), layer_height: String(ctx.layerHeight) } : {};
  const own = chosen ? chosen.own : { ...frozen, ...deltas };
  const created = source ? await profileCreation(() => {
    const { auto: _auto, ...copy } = structuredClone(source) as UserPreset & { auto?: object }, overlay = { ...source.overlay, ...own };
    const auto = type !== 'process' ? {} : autoOf(source, chosen ? chosen.rows : kind === 'regular' ? [] : scales);
    if (chosen) for (const r of chosen.rows) delete overlay[r.key];
    return { ...copy, id: `profile:${crypto.randomUUID()}`, kind: 'profile', name: name.trim(), revision: 0, aliases: [], archived: undefined, overlay, overrides: overlay,
      ...auto } as UserPreset;
  }).commit()
    : await createProfileFromBase(type, resolved.base.name, name.trim(), { ...resolved.config, ...deltas });
  const printer = chosen && printerWith(getState(), chosen.toPrinter);
  if (printer) await commitProfileRecords([printer]);
  switchPreset(type, slot, created, 'discard');

  if (chosen && Object.keys(chosen.kept).length) setState((st) => ({ dirty: { ...st.dirty, process: { ...st.dirty.process, ...chosen.kept } } }));
  toast(`Saved as ${name.trim()}`, { label: 'Undo', run: () => {
    switchPreset(type, slot, key, 'discard');
    restoreChanges(type, slot, key, before);
    void archiveProfiles(type, [created]).catch((e) => toast(message(e)));
  } });
}



export function createPrintProfile(kind: 'dynamic' | 'regular', name: string): Promise<string> {
  return profileCreation((s) => {
    const everyday = s.userPresets.find((p) => p.type === 'process' && !p.archived && p.name === 'Everyday' && scalesOf(p).length);
    return { id: `profile:${crypto.randomUUID()}`, type: 'process', kind: 'profile', name: name.trim(), revision: 0, inherits: 'Auto',
      base: { name: 'Auto', engineName: 'Auto', version: 'auto:1' }, overlay: {}, overrides: {},
      ...(kind === 'dynamic' ? { auto: { schema: 3, scales: everyday ? scalesOf(everyday) : [...DEFAULT_SCALES] } } : {}) } as UserPreset;
  }).commit();
}
