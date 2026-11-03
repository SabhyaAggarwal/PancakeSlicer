import { strToU8, unzipSync, zipSync, type Zippable } from 'fflate';
import type { Config, ConfigValue, PresetType, SystemPreset, UserPreset } from './api/types';
import { normalizeConfigValue, unescapeConfigString } from './config-values';
import { isHidden, OPTIONS } from './options';
import { compatible, getState, listFor, PRESET_META, resolvePreset, type State } from './state/store';
import { profileKey } from './profiles/identity';
import { validApplicability } from './profiles/applicability';
import { baseVersion, normalizeUserPreset, saveProfile, storedProfile } from './profiles/model';

export const PRESET_VERSION = '02.08.01.55';
const SETTINGS_ID: Record<PresetType, string> = { machine: 'printer_settings_id', process: 'print_settings_id', filament: 'filament_settings_id' };
const DIR: Record<PresetType, string> = { machine: 'printer', process: 'process', filament: 'filament' };

const MAX_FILE = 64 << 20, MAX_ENTRY = 2 << 20, MAX_ENTRIES = 2000, MAX_TOTAL = 64 << 20;

function orcaValue(key: string, value: ConfigValue): string | string[] {
  const v = normalizeConfigValue(key, value);
  if (Array.isArray(v)) return v.map(String);
  if (typeof v === 'boolean') return v ? '1' : '0';
  return String(v);
}

export function toOrcaJson(p: UserPreset, s: State = getState()): Record<string, string | string[]> {
  const withProfile = { ...s, userPresets: [...s.userPresets.filter((u) => u.type !== p.type || profileKey(u) !== profileKey(p)), p] };
  const { config, base } = resolvePreset(withProfile, p.type, profileKey(p));
  const out: Record<string, string | string[]> = {};
  for (const [key, value] of Object.entries({ ...config, ...p.metadata }))
    if (!PRESET_META.test(key)) out[key] = orcaValue(key, value);
  for (const key of ['setting_id', 'base_id', 'filament_id', 'type'])
    if (p.metadata?.[key] !== undefined) out[key] = orcaValue(key, p.metadata[key]);
  if (!out.filament_id && base?.filament_id) out.filament_id = base.filament_id;
  if (out.compatible_printers === undefined && base?.compatible_printers) out.compatible_printers = base.compatible_printers;
  Object.assign(out, { from: 'User', inherits: p.parent ?? (p.metadata?.inherits === '' ? '' : p.base?.name ?? p.inherits), name: p.name,
    version: String(p.metadata?.version ?? PRESET_VERSION) });
  out[SETTINGS_ID[p.type]] = p.type === 'filament' ? [p.name] : p.name;
  out.orca_web_profile = JSON.stringify({schema:1,profile:{...storedProfile(p),snapshot:p.base ? {base:p.base,config} : p.snapshot}});
  return out;
}

export const presetJsonText = (p: UserPreset, s: State = getState()) => JSON.stringify(toOrcaJson(p, s), null, '\t') + '\n';

export interface ParsedPreset {
  file: string;
  name: string;
  type?: PresetType;
  inherits: string;
  overrides: Config;
  dropped: string[];
  error?: string;
  from?: string;
  printers: string[];
  metadata?: Config;
  appProfile?: UserPreset;
}

function typeOf(raw: Record<string, unknown>): PresetType | undefined {
  if ('printer_settings_id' in raw) return 'machine';
  if ('print_settings_id' in raw) return 'process';
  if ('filament_settings_id' in raw) return 'filament';
  return raw.type === 'machine' || raw.type === 'process' || raw.type === 'filament' ? raw.type : undefined;
}

export function parsePresetJson(text: string, file: string): ParsedPreset {
  const base: ParsedPreset = { file, name: file.replace(/^.*\//, '').replace(/\.json$/i, ''), inherits: '', overrides: {}, dropped: [], printers: [] };
  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch {
    return { ...base, error: 'not valid JSON' };
  }
  if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return { ...base, error: 'not a preset object' };
  const r = raw as Record<string, unknown>;
  const str = (v: unknown) => (Array.isArray(v) ? String(v[0] ?? '') : v == null ? '' : String(v));
  const parsed: ParsedPreset = { ...base, name: str(r.name) || base.name, inherits: str(r.inherits), type: typeOf(r), from: str(r.from), metadata: {} };
  if (!parsed.type) return { ...parsed, error: 'unknown preset type (no printer_settings_id, print_settings_id or filament_settings_id)' };
  if (!/^\d+(\.\d+){1,3}/.test(str(r.version))) return { ...parsed, error: 'missing or invalid "version"' };
  if (r.orca_web_profile !== undefined) {
    try {
      const app = typeof r.orca_web_profile === 'string' ? JSON.parse(r.orca_web_profile) : r.orca_web_profile;
      const p = app?.profile;
      if (app?.schema !== 1 || !p || p.type !== parsed.type || typeof p.name !== 'string' || !p.base || typeof p.base.name !== 'string'
        || typeof p.base.version !== 'string' || !p.overlay || typeof p.overlay !== 'object' || Array.isArray(p.overlay)
        || p.applicability !== undefined && !validApplicability(p.applicability)) throw new Error('invalid app metadata');
      parsed.appProfile = normalizeUserPreset(p);
    } catch { return {...parsed,error:'The saved profile metadata is invalid. Repair or re-export the profile.'}; }
  }
  for (const [key, value] of Object.entries(r)) {
    if (key === 'orca_web_profile') continue;
    if (!(typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean'
      || Array.isArray(value) && (value.every((x) => typeof x === 'string') || value.every((x) => typeof x === 'number')))) {
      parsed.dropped.push(key);
      continue;
    }
    const native = value as ConfigValue;
    if (PRESET_META.test(key) || !OPTIONS[key] || isHidden(key)) {
      parsed.metadata![key] = native;
      if (!PRESET_META.test(key)) parsed.dropped.push(key);
      continue;
    }
    const text = key === 'bed_custom_texture' || key === 'bed_custom_model' ? unescapeConfigString(String(value)) : native;
    parsed.overrides[key] = normalizeConfigValue(key, text);
  }
  const printers = r.compatible_printers;
  parsed.printers = Array.isArray(printers) ? printers.map(String) : [];
  return parsed;
}

export async function readPresetFile(file: File): Promise<ParsedPreset[]> {
  if (file.size > MAX_FILE) return [{ ...emptyParsed(file.name), error: `larger than ${MAX_FILE >> 20} MB` }];
  if (/\.json$/i.test(file.name)) return [parsePresetJson(await file.text(), file.name)];
  if (!/\.(zip|bbscfg|orca_printer|orca_filament|orca_bundle)$/i.test(file.name)) return [{ ...emptyParsed(file.name), error: 'not a preset file' }];
  let entries: Record<string, Uint8Array>;
  let count = 0, total = 0, tooLarge = '';
  try {
    entries = unzipSync(new Uint8Array(await file.arrayBuffer()), {
      filter: (f) => {
        if (!/\.json$/i.test(f.name) || /(^|\/)bundle_structure\.json$/i.test(f.name) || /(^|\/)__MACOSX\//.test(f.name)) return false;
        if (++count > MAX_ENTRIES || f.originalSize > MAX_ENTRY || (total += f.originalSize) > MAX_TOTAL) { tooLarge ||= f.name; return false; }
        return true;
      },
    });
  } catch (e) {
    return [{ ...emptyParsed(file.name), error: `not a readable archive (${e instanceof Error ? e.message : e})` }];
  }
  const out = Object.entries(entries).map(([name, data]) =>
    parsePresetJson(new TextDecoder().decode(data), `${file.name}/${name}`));
  if (tooLarge) out.push({ ...emptyParsed(`${file.name}/${tooLarge}`), error: 'the archive exceeds the size or entry limits; the rest was skipped' });
  return out;
}

const emptyParsed = (file: string): ParsedPreset => ({ file, name: file.replace(/^.*\//, ''), inherits: '', overrides: {}, dropped: [], printers: [] });

export interface ImportPlan {
  parsed: ParsedPreset;
  preset?: UserPreset;
  base?: SystemPreset;
  status: 'new' | 'replace' | 'skip';
  note?: string;
}

export function planImport(s: State, parsed: ParsedPreset[]): ImportPlan[] {
  const working = { ...s, userPresets: [...s.userPresets] };
  const plans = new Map<ParsedPreset, ImportPlan>(), visiting = new Set<ParsedPreset>();
  const id = (p: ParsedPreset) => `${p.type}\0${p.appProfile?.id ?? p.name}`;
  const first = new Map<string, ParsedPreset>();
  for (const p of parsed) if (!first.has(id(p))) first.set(id(p), p);
  const build = (p: ParsedPreset): ImportPlan => {
    const ready = plans.get(p);
    if (ready) return ready;
    const skip = (note?: string): ImportPlan => { const plan: ImportPlan = { parsed: p, status: 'skip', note }; plans.set(p, plan); return plan; };
    if (visiting.has(p)) return skip('cyclic parent');
    if (p.error || !p.type) return skip(p.error);
    if (first.get(id(p)) !== p) return skip('duplicate');
    const type = p.type;
    if (p.appProfile) {
      const profile = p.appProfile;
      const same = working.userPresets.find(u => u.type === type && profileKey(u) === profileKey(profile));
      const plan: ImportPlan = {parsed:p,preset:profile,status:same ? 'replace' : 'new'};
      working.userPresets = [...working.userPresets.filter(u => u.type !== type || profileKey(u) !== profileKey(profile)),profile];
      plans.set(p,plan); return plan;
    }
    visiting.add(p);
    if (p.from?.toLowerCase() === 'system') {
      const incoming = first.get(`${type}\0${p.inherits}`);
      if (incoming && incoming !== p && build(incoming).status === 'skip') return skip('Base unavailable');
      const inherited = p.inherits ? resolvePreset(working, type, p.inherits).base : undefined;
      const current = listFor(working, type).find((x) => x.name === p.name);
      const selected = type === 'machine' ? working.printer : type === 'process' ? working.process : working.filamentSlots[0] ?? '';
      const anchor = inherited ?? current ?? resolvePreset(working, type, selected).base ?? listFor(working, type)[0];
      if (working.userPresets.some((u) => u.type === type && u.name === p.name)) return skip('Name used by a profile');
      const config = { ...anchor?.config, ...inherited?.config, ...p.overrides };
      const rawPrinters = p.overrides.compatible_printers ?? p.metadata?.compatible_printers;
      for (const [key, value] of Object.entries(p.metadata ?? {})) if (!PRESET_META.test(key)) config[key] = value;
      const base: SystemPreset = { name: p.name, type, vendor: String(p.metadata?.vendor ?? inherited?.vendor ?? current?.vendor ?? anchor?.vendor ?? 'Custom'),
        version: String(p.metadata?.version ?? PRESET_VERSION), setting_id: String(p.metadata?.setting_id ?? ''),
        base_id: String(p.metadata?.base_id ?? ''), filament_id: String(p.metadata?.filament_id ?? '') || undefined,
        inherits: p.inherits ? [p.inherits] : [], config, compatible_printers: rawPrinters === undefined ? inherited?.compatible_printers : p.printers,
        compatible_evaluated: rawPrinters === undefined ? inherited?.compatible_evaluated : false,
        compatible_prints: Array.isArray(config.compatible_prints) && config.compatible_prints.length ? config.compatible_prints.map(String) : inherited?.compatible_prints,
        model: type === 'machine' ? String(config.printer_model ?? anchor?.model ?? '') : undefined,
        imported: true, engine_base: anchor?.engine_base ?? anchor?.name };
      const key = type === 'machine' ? 'machines' : type === 'process' ? 'processes' : 'filaments';
      working[key] = [...working[key].filter((b) => b.name !== base.name), base];
      const plan: ImportPlan = { parsed: p, base, status: current ? 'replace' : 'new', note: 'Add to catalog' };
      plans.set(p, plan); visiting.delete(p); return plan;
    }
    if (listFor(s, type).some((x) => x.name === p.name)) return skip('Already in catalog');
    let inherits = p.inherits, note: string | undefined;
    if (inherits) {
      const incoming = first.get(`${type}\0${inherits}`);
      if (incoming && build(incoming).status === 'skip') return skip(`parent "${inherits}" not available`);
      if (!resolvePreset(working, type, inherits).base) return skip(`parent "${inherits}" not available`);
    } else {
      const printer = p.printers.find((n) => s.machines.some((m) => m.name === n)) ?? s.printer;
      const anchors = type === 'machine' ? s.machines : compatible(s, type, printer);
      const kind = String(p.overrides.filament_type ?? '');
      const anchor = (type === 'machine' && anchors.find((m) => String(m.config.printer_model ?? '') === String(p.overrides.printer_model ?? '')))
        || (type === 'filament' && anchors.find((f) => String(f.config.filament_type) === kind && /Generic/.test(f.name)))
        || (type === 'filament' && anchors.find((f) => String(f.config.filament_type) === kind))
        || anchors.find((x) => x.name === (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[0]))
        || anchors[0];
      if (!anchor) return skip('no compatible parent');
      inherits = anchor.name;
      note = 'no parent: full values';
    }
    const { config, base } = resolvePreset(working, type, inherits);
    const preset = saveProfile(working, type, p.name, inherits, { ...config, ...p.overrides });
    preset.metadata = p.metadata;
    if (!p.inherits) {
      preset.overlay = { ...p.overrides };
      preset.overrides = preset.overlay;
      preset.base = { name: base!.name, version: baseVersion(base!) };
      preset.baseline = Object.fromEntries(Object.keys(preset.overlay).map((key) => [key, base!.config[key] ?? null]));
    }
    const exists = s.userPresets.some((u) => u.type === type && u.name === p.name);
    const retained = p.dropped.length ? `${p.dropped.length} uneditable fields retained for export` : '';
    const plan: ImportPlan = { parsed: p, status: exists ? 'replace' : 'new',
      note: [note, retained].filter(Boolean).join(', ') || undefined, preset };
    working.userPresets = [...working.userPresets.filter((u) => u.type !== type || u.name !== p.name), preset];
    plans.set(p, plan);
    visiting.delete(p);
    return plan;
  };
  return parsed.map(build);
}

export async function applyImportPlans(plans: ImportPlan[]): Promise<{ profiles: number; bases: number }> {
  const accepted = plans.filter(p => p.status !== 'skip'), bases = accepted.flatMap(p => p.base ? [p.base] : []), profiles = accepted.flatMap(p => p.preset ? [p.preset] : []);
  if(profiles.length || bases.length){const {commitProfileRecords}=await import('./profiles/transactions');await commitProfileRecords(profiles,{bases,operationId:`import:${crypto.randomUUID()}`});}
  return {profiles:profiles.length,bases:bases.length};
}

function download(name: string, data: Uint8Array | string, mime: string) {
  const url = URL.createObjectURL(new Blob([data as BlobPart], { type: mime }));
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 30_000);
}

const fileName = (name: string) => name.replace(/[<>:"/\\|?*\u0000-\u001f]/g, '_');

export const exportPresetJson = (p: UserPreset, s: State = getState()) => download(`${fileName(p.name)}.json`, presetJsonText(p, s), 'application/json');

export type ExportKind = 'printer_bundle' | 'filament_bundle' | 'machine' | 'filament' | 'process';

const stamp = () => new Date().toISOString().replace(/\D/g, '').slice(0, 14);

export function printerBundle(s: State, printer: UserPreset): { name: string; data: Uint8Array } {
  const files: Zippable = {};
  const add = (p: UserPreset) => { const path = `${DIR[p.type]}/${fileName(p.name)}.json`; files[path] = strToU8(presetJsonText(p, s)); return path; };
  const fits = (p: UserPreset) => compatible(s, p.type as 'process' | 'filament', printer.inherits).some((x) => x.name === p.inherits);
  const structure = {
    bundle_id: `offline_${printer.name}_${stamp()}`, bundle_type: 'printer config bundle', printer_preset_name: printer.name, version: '',
    printer_config: [add(printer)],
    filament_config: s.userPresets.filter((p) => p.type === 'filament' && fits(p)).map(add),
    process_config: s.userPresets.filter((p) => p.type === 'process' && fits(p)).map(add),
  };
  files['bundle_structure.json'] = strToU8(JSON.stringify(structure));
  return { name: `${fileName(printer.name)}.orca_printer`, data: zipSync(files) };
}

export function filamentBundle(s: State, filament: string, presets: UserPreset[]): { name: string; data: Uint8Array } {
  const files: Zippable = {};
  const vendors = new Map<string, string[]>();
  for (const p of presets) {
    const vendor = resolvePreset(s, 'filament', p.name).base?.vendor || 'Custom';
    const path = `${vendor}/${fileName(p.name)}.json`;
    files[path] = strToU8(presetJsonText(p, s));
    vendors.set(vendor, [...(vendors.get(vendor) ?? []), path]);
  }
  files['bundle_structure.json'] = strToU8(JSON.stringify({
    bundle_id: `offline_${filament}_${stamp()}`, bundle_type: 'filament config bundle', filament_name: filament, version: '',
    printer_vendor: [...vendors].map(([vendor, filament_path]) => ({ vendor, filament_path })),
  }));
  return { name: `${fileName(filament)}.orca_filament`, data: zipSync(files) };
}

export function presetZip(presets: UserPreset[], type: PresetType, s: State = getState()): { name: string; data: Uint8Array } {
  const files: Zippable = {};
  for (const p of presets) files[`${fileName(p.name)}.json`] = strToU8(presetJsonText(p, s));
  return { name: `${type === 'machine' ? 'Printer' : type === 'filament' ? 'Filament' : 'Process'} presets.zip`, data: zipSync(files) };
}

export function configBundle(s: State, presets: UserPreset[]): { name: string; data: Uint8Array } {
  const files: Zippable = {};
  for (const p of presets) files[`${DIR[p.type]}/${fileName(p.name)}.json`] = strToU8(presetJsonText(p, s));
  return { name: 'profiles.bbscfg', data: zipSync(files) };
}

export const downloadArchive = ({ name, data }: { name: string; data: Uint8Array }) => download(name, data, 'application/zip');

export const filamentAlias = (name: string) => name.replace(/\s*@.*$/, '');
