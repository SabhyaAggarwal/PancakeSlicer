import type { PresetType } from '../api/types';

export interface WorksWithRule {
  schema: 2;
  printers?: { models: string[] } | { printerIds: string[] };
  nozzle?: { sizes: number[] } | { min: number; max: number };
  filaments?: { types: string[] } | { profileIds: string[] };
  prints?: { profileIds: string[] };
  exclude?: string[];
}
export interface PrinterFacts { id?: string; name?: string; printerModel: string; nozzle: number }
export interface FilamentFacts { id?: string; type: string }

const strings = (v: unknown): v is string[] => Array.isArray(v) && v.length > 0 && v.every((x) => typeof x === 'string' && x !== '');
const sizes = (v: unknown): v is number[] => Array.isArray(v) && v.length > 0 && v.every((x) => typeof x === 'number' && x > 0);
export function validWorksWith(value: unknown, type: PresetType): value is WorksWithRule {
  if (!value || typeof value !== 'object') return false;
  const r = value as Record<string, unknown>, one = (c: unknown, ...shapes: ((o: Record<string, unknown>) => boolean)[]) =>
    c === undefined || !!c && typeof c === 'object' && shapes.some((ok) => ok(c as Record<string, unknown>));
  const keys = (o: Record<string, unknown>, ...k: string[]) => Object.keys(o).length === k.length && k.every((x) => x in o);
  return r.schema === 2
    && Object.keys(r).every((k) => ['schema', 'printers', 'nozzle', 'filaments', 'prints', 'exclude'].includes(k))
    && one(r.printers, (o) => keys(o, 'models') && strings(o.models), (o) => keys(o, 'printerIds') && strings(o.printerIds))
    && one(r.nozzle, (o) => keys(o, 'sizes') && sizes(o.sizes),
      (o) => keys(o, 'min', 'max') && typeof o.min === 'number' && typeof o.max === 'number' && o.min > 0 && o.min <= o.max)
    && (type === 'filament' ? r.filaments === undefined : one(r.filaments, (o) => keys(o, 'types') && strings(o.types), (o) => keys(o, 'profileIds') && strings(o.profileIds)))
    && (type === 'filament' ? one(r.prints, (o) => keys(o, 'profileIds') && strings(o.profileIds)) : r.prints === undefined)
    && (r.exclude === undefined || strings(r.exclude))
    && (type !== 'machine' || r.printers === undefined && r.nozzle === undefined && r.exclude === undefined);
}

const near = (a: number, b: number) => Math.abs(a - b) < 0.0005;
export function matchesNozzle(rule: WorksWithRule, nozzle: number): boolean {
  const n = rule.nozzle;
  return !n || ('sizes' in n ? n.sizes.some((s) => near(s, nozzle)) : nozzle >= n.min - 0.0005 && nozzle <= n.max + 0.0005);
}
export function matchesPrinter(rule: WorksWithRule, p: PrinterFacts): boolean {
  const c = rule.printers;
  if (p.id && rule.exclude?.includes(p.id)) return false;
  if (c && ('models' in c ? !c.models.includes(p.printerModel) : !p.id || !c.printerIds.includes(p.id))) return false;
  return matchesNozzle(rule, p.nozzle);
}
export function matchesFilament(rule: WorksWithRule, f: FilamentFacts): boolean {
  const c = rule.filaments;
  return !c || ('types' in c ? c.types.some((t) => t.toLowerCase() === f.type.toLowerCase()) : !!f.id && c.profileIds.includes(f.id));
}
export const matchesPrint = (rule: WorksWithRule, printId: string | undefined) => !rule.prints || !!printId && rule.prints.profileIds.includes(printId);

export function fromApplicability1(value: { mode: 'inherit' } | { mode: 'only'; printerIds: string[] }): WorksWithRule | undefined {
  return value.mode === 'only' ? { schema: 2, printers: { printerIds: value.printerIds } } : undefined;
}

const quote = (s: string) => `"${s.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`;
const mm = (n: number) => String(Math.round(n * 10000) / 10000);
export function orcaCompatibility(rule: WorksWithRule, printers: PrinterFacts[], printNames: Record<string, string> = {}) {
  const out: { compatible_printers: string[]; compatible_printers_condition: string; compatible_prints?: string[] } =
    { compatible_printers: [], compatible_printers_condition: '' };
  if (rule.prints) out.compatible_prints = rule.prints.profileIds.map((id) => printNames[id]).filter((n): n is string => !!n);
  if (rule.printers && 'printerIds' in rule.printers) {
    out.compatible_printers = printers.filter((p) => p.name && matchesPrinter(rule, p)).map((p) => p.name!);
    return out;
  }
  const terms: string[] = [];
  if (rule.printers) terms.push(`(${rule.printers.models.map((m) => `printer_model==${quote(m)}`).join(' or ')})`);
  const n = rule.nozzle;
  if (n) terms.push('sizes' in n
    ? `(${n.sizes.map((s) => `(nozzle_diameter[0]>=${mm(s - 0.0005)} and nozzle_diameter[0]<=${mm(s + 0.0005)})`).join(' or ')})`
    : `nozzle_diameter[0]>=${mm(n.min - 0.0005)} and nozzle_diameter[0]<=${mm(n.max + 0.0005)}`);
  for (const id of rule.exclude ?? []) {
    const name = printers.find((p) => p.id === id)?.name;
    if (name) terms.push(`printer_settings_id!=${quote(name)}`);
  }
  out.compatible_printers_condition = terms.join(' and ');
  return out;
}
