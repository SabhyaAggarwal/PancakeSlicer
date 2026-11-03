import type { PresetType, SystemPreset } from '../api/types';
import { resolveProfile, type ProfileCatalog } from './model';

export interface Compatibility { ok: boolean; reason: string }
export function isCompatible(p: SystemPreset, printer: string, process?: string): boolean {
  if (!p.compatible_evaluated) return !p.compatible_printers?.length || p.compatible_printers.includes(printer);
  return !!p.compatible_printers?.includes(printer) && (!process || !p.compatible_prints || p.compatible_prints.includes(process));
}

const nozzleOf = (name: string) => /(?:^|\s)(\d+(?:\.\d+)?)\s*(?:mm\s*)?nozzle\b/i.exec(name)?.[1];
const machineOf = (name: string) => name.replace(/\s+\d+(?:\.\d+)?\s*(?:mm\s*)?nozzle\b/i, '').trim();

export function selectCompatibility(s: ProfileCatalog & { printer: string; process: string }, type: PresetType, name: string,
  printer = s.printer, nozzle?: string | number): Compatibility {
  const profile = resolveProfile(s, type, name);
  if (!profile.base && !profile.user) return { ok: false, reason: 'Unavailable for this machine' };
  if (type === 'machine') return { ok: true, reason: '' };
  const machine = resolveProfile(s, 'machine', printer);
  const printerName = machine.base?.name ?? printer;
  const process = type === 'filament' ? resolveProfile(s, 'process', s.process).base?.name ?? s.process : undefined;
  const metadata = profile.user?.metadata;
  const explicit = metadata?.compatible_printers ?? profile.user?.overrides.compatible_printers;
  const prints = metadata?.compatible_prints ?? profile.user?.overrides.compatible_prints;
  const p: SystemPreset = { ...profile.base!, name, type,
    compatible_printers: explicit === undefined ? profile.base?.compatible_printers : Array.isArray(explicit) ? explicit.map(String) : [],
    compatible_evaluated: explicit === undefined ? profile.base?.compatible_evaluated : false,
    compatible_prints: prints === undefined ? profile.base?.compatible_prints : Array.isArray(prints) ? prints.map(String) : [],
  };
  const requested = nozzle === undefined ? undefined : Number(nozzle);
  const variant = requested === undefined ? printerName : s.machines.find((m) =>
    (m.model ?? machineOf(m.name)) === (machine.base?.model ?? machineOf(printerName))
    && Number(Array.isArray(m.config.nozzle_diameter) ? m.config.nozzle_diameter[0] : m.config.nozzle_diameter ?? nozzleOf(m.name)) === requested)?.name;
  const target = variant ?? printerName;
  const allowed = p.compatible_printers ?? [];
  const currentNozzle = requested ?? Number(nozzleOf(target));
  const sameMachine = allowed.filter((n) => machineOf(n) === machineOf(printerName));
  const required = [...new Set(sameMachine.map(nozzleOf).filter((n): n is string => !!n))];
  const wrongNozzle = required.length > 0 && Number.isFinite(currentNozzle) && !required.some((n) => Number(n) === currentNozzle);
  if (!wrongNozzle && isCompatible(p, target, process) && (!prints || !process || !p.compatible_prints?.length || p.compatible_prints.includes(process)))
    return { ok: true, reason: '' };
  if (wrongNozzle) return { ok: false, reason: `Needs ${required.join(' / ')} nozzle` };
  if (allowed.includes(target) && process && p.compatible_prints && !p.compatible_prints.includes(process))
    return { ok: false, reason: 'Other process' };
  const vendors = allowed.map((n) => s.machines.find((m) => m.name === n)?.vendor).filter(Boolean);
  if (vendors.length && machine.base?.vendor && vendors.every((v) => v !== machine.base!.vendor))
    return { ok: false, reason: 'Other vendor' };
  return { ok: false, reason: allowed.length ? 'Other printer' : 'Unavailable for this machine' };
}
