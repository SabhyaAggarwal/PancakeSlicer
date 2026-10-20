import { strToU8, unzipSync, zipSync, type Zippable } from 'fflate';
import type { Config, ConfigValue, PresetType, UserPreset } from './api/types';
import { normalizeConfigValue, unescapeConfigString } from './config-values';
import { isHidden, OPTIONS } from './options';
import { compatible, listFor, PRESET_META, resolvePreset, type State } from './state/store';

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

export function toOrcaJson(p: UserPreset): Record<string, string | string[]> {
  const out: Record<string, string | string[]> = { from: 'User', inherits: p.inherits, name: p.name, version: PRESET_VERSION };
  out[SETTINGS_ID[p.type]] = p.type === 'filament' ? [p.name] : p.name;
  for (const [key, value] of Object.entries(p.overrides)) if (!PRESET_META.test(key)) out[key] = orcaValue(key, value);
  return out;
}

export const presetJsonText = (p: UserPreset) => JSON.stringify(toOrcaJson(p), null, '\t') + '\n';

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
  const parsed: ParsedPreset = { ...base, name: str(r.name) || base.name, inherits: str(r.inherits), type: typeOf(r), from: str(r.from) };
  if (!parsed.type) return { ...parsed, error: 'unknown preset type (no printer_settings_id, print_settings_id or filament_settings_id)' };
  if (!/^\d+(\.\d+){1,3}/.test(str(r.version))) return { ...parsed, error: 'missing or invalid "version"' };
  for (const [key, value] of Object.entries(r)) {
    if (PRESET_META.test(key)) continue;
    if (!OPTIONS[key] || isHidden(key) || !(typeof value === 'string' || (Array.isArray(value) && value.every((x) => typeof x === 'string')))) {
      parsed.dropped.push(key);
      continue;
    }
    const text = key === 'bed_custom_texture' || key === 'bed_custom_model' ? unescapeConfigString(value as string) : value as string | string[];
    parsed.overrides[key] = normalizeConfigValue(key, text);
  }
  const printers = r.compatible_printers;
  parsed.printers = Array.isArray(printers) ? printers.map(String) : [];
  return parsed;
}

export async function readPresetFile(file: File): Promise<ParsedPreset[]> {
  if (file.size > MAX_FILE) return [{ ...emptyParsed(file.name), error: `larger than ${MAX_FILE >> 20} MB` }];
  if (/\.json$/i.test(file.name)) return [parsePresetJson(await file.text(), file.name)];
  if (!/\.(zip|orca_printer|orca_filament|orca_bundle)$/i.test(file.name)) return [{ ...emptyParsed(file.name), error: 'not a preset file' }];
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
  status: 'new' | 'replace' | 'skip';
  note?: string;
}

export function planImport(s: State, parsed: ParsedPreset[]): ImportPlan[] {
  const seen = new Set<string>();
  return parsed.map((p): ImportPlan => {
    if (p.error || !p.type) return { parsed: p, status: 'skip', note: p.error };
    const type = p.type;
    if (listFor(s, type).some((x) => x.name === p.name)) return { parsed: p, status: 'skip', note: 'system preset' };
    if (seen.has(`${type}\0${p.name}`)) return { parsed: p, status: 'skip', note: 'duplicate' };
    seen.add(`${type}\0${p.name}`);
    let inherits = p.inherits, note: string | undefined;
    if (inherits) {
      const parent = resolvePreset(s, type, inherits);
      if (!parent.base) return { parsed: p, status: 'skip', note: `parent "${inherits}" not available` };
      if (parent.user) { inherits = parent.base.name; p.overrides = { ...parent.user.overrides, ...p.overrides }; note = `flattened onto "${inherits}"`; }
    } else {
      const printer = p.printers.find((n) => s.machines.some((m) => m.name === n)) ?? s.printer;
      const anchors = type === 'machine' ? s.machines : compatible(s, type, printer);
      const kind = String(p.overrides.filament_type ?? '');
      const anchor = (type === 'machine' && anchors.find((m) => String(m.config.printer_model ?? '') === String(p.overrides.printer_model ?? '')))
        || (type === 'filament' && anchors.find((f) => String(f.config.filament_type) === kind && /Generic/.test(f.name)))
        || (type === 'filament' && anchors.find((f) => String(f.config.filament_type) === kind))
        || anchors.find((x) => x.name === (type === 'machine' ? s.printer : type === 'process' ? s.process : s.filamentSlots[0]))
        || anchors[0];
      if (!anchor) return { parsed: p, status: 'skip', note: 'no compatible parent' };
      inherits = anchor.name;
      note = 'no parent: full values';
    }
    const exists = s.userPresets.some((u) => u.type === type && u.name === p.name);
    const dropped = p.dropped.length ? `${p.dropped.length} unknown key${p.dropped.length === 1 ? '' : 's'} dropped` : '';
    return {
      parsed: p, status: exists ? 'replace' : 'new', note: [note, dropped].filter(Boolean).join(', ') || undefined,
      preset: { name: p.name, type, inherits, overrides: p.overrides },
    };
  });
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

export const exportPresetJson = (p: UserPreset) => download(`${fileName(p.name)}.json`, presetJsonText(p), 'application/json');

export type ExportKind = 'printer_bundle' | 'filament_bundle' | 'machine' | 'filament' | 'process';

const stamp = () => new Date().toISOString().replace(/\D/g, '').slice(0, 14);

export function printerBundle(s: State, printer: UserPreset): { name: string; data: Uint8Array } {
  const files: Zippable = {};
  const add = (p: UserPreset) => { const path = `${DIR[p.type]}/${fileName(p.name)}.json`; files[path] = strToU8(presetJsonText(p)); return path; };
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
    files[path] = strToU8(presetJsonText(p));
    vendors.set(vendor, [...(vendors.get(vendor) ?? []), path]);
  }
  files['bundle_structure.json'] = strToU8(JSON.stringify({
    bundle_id: `offline_${filament}_${stamp()}`, bundle_type: 'filament config bundle', filament_name: filament, version: '',
    printer_vendor: [...vendors].map(([vendor, filament_path]) => ({ vendor, filament_path })),
  }));
  return { name: `${fileName(filament)}.orca_filament`, data: zipSync(files) };
}

export function presetZip(presets: UserPreset[], type: PresetType): { name: string; data: Uint8Array } {
  const files: Zippable = {};
  for (const p of presets) files[`${fileName(p.name)}.json`] = strToU8(presetJsonText(p));
  return { name: `${type === 'machine' ? 'Printer' : type === 'filament' ? 'Filament' : 'Process'} presets.zip`, data: zipSync(files) };
}

export const downloadArchive = ({ name, data }: { name: string; data: Uint8Array }) => download(name, data, 'application/zip');

export const filamentAlias = (name: string) => name.replace(/\s*@.*$/, '');
