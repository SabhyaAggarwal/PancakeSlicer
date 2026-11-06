
import type { Config, PresetType, SystemPreset } from '../api/types';
import { normalizeConfigValue } from '../config-values';
import { resolveProfile } from './model';
import { profileContext, type ContextProfile, type ProfileContextState } from './context';
import { evaluateProfileCondition } from './condition';
import { hasWorksWithRule, worksWithReason } from './applicability';
export interface Compatibility { ok: boolean; reason: string; unknown?: boolean }
const strings = (key: string, value: Config[string] | undefined): string[] => {
  if (value === undefined || value === '') return [];
  const native = normalizeConfigValue(key,value); return Array.isArray(native) ? native.map(String) : [String(native)];
};
const allowed = (): Compatibility => ({ok:true,reason:''});
const denied = (reason: string, unknown = false): Compatibility => ({ok:false,reason,...(unknown ? {unknown:true} : {})});

export function isCompatible(p: SystemPreset, printer: string, process?: string): boolean {
  if (p.compatible_evaluated) return !!p.compatible_printers?.includes(printer) && (!process || !p.compatible_prints || p.compatible_prints.includes(process));
  const list = strings('compatible_printers',p.config.compatible_printers ?? p.compatible_printers);

  return list.length ? list.includes(printer) : !p.config.compatible_printers_condition;
}
function nativeConstraint(config: Config, kind: 'printers' | 'prints', active: ContextProfile): Compatibility {
  const key = kind === 'printers' ? 'compatible_printers' : 'compatible_prints', list = strings(key, config[key]);

  if (list.length) return list.includes(active.name ?? '') || kind === 'printers' && !!active.source && !!active.parentName && list.includes(active.parentName)
    ? allowed() : denied(kind === 'printers' ? 'Other printer' : 'Other print settings');
  const expression = String(config[`${key}_condition`] ?? '');
  if (!expression) return allowed();
  const env: Config = kind === 'printers' ? {...active.config,printer_preset:active.name ?? '',num_extruders:[active.config.nozzle_diameter ?? []].flat().length} : active.config;
  const result = evaluateProfileCondition(expression,env);
  return !result.ok ? denied(`Compatibility needs repair: ${result.error}`,true) : result.value ? allowed() : denied(kind === 'printers' ? 'Printer settings do not match' : 'Print settings do not match');
}

function resolvable(s: ProfileContextState, type: PresetType, key: string, printer: string, nozzle?: string | number) {
  const profile = resolveProfile(s,type,key);
  if (profile.issue) return {profile, failure: denied(profile.error ?? 'Profile unavailable',true)};
  if (type === 'machine') return {profile};
  const context = profileContext(s,{printer,nozzle});
  if (context.printer.issue) return {profile, context, failure: denied(context.printer.error ?? 'Printer unavailable',true)};
  if (type === 'filament' && context.process.issue) return {profile, context, failure: denied(context.process.error ?? 'Print settings unavailable',true)};
  return {profile, context};
}

export function selectNativeCompatibility(s: ProfileContextState, type: PresetType, key: string, printer = s.printer, nozzle?: string | number): Compatibility {
  const {profile, context, failure} = resolvable(s,type,key,printer,nozzle);
  if (failure) return failure;
  if (!context) return allowed();

  if (type === 'process' && (profile.source as { auto?: unknown } | undefined)?.auto) return allowed();


  if (type === 'filament' && profile.source && profile.source.kind !== 'master') return allowed();
  const config = {...profile.config};

  if (config.compatible_printers === undefined && !profile.base?.compatible_evaluated && profile.base?.compatible_printers)
    config.compatible_printers = profile.base.compatible_printers;
  const printers = nativeConstraint(config,'printers',context.printer);
  if (!printers.ok) return printers;


  const raw = strings('compatible_printers',config.compatible_printers), expression = String(config.compatible_printers_condition ?? '');
  const overridden = !!profile.source && ['compatible_printers','compatible_printers_condition'].some(key => key in (profile.source!.overlay ?? profile.source!.overrides) || key in (profile.source!.metadata ?? {}));
  if (!raw.length && !expression && !overridden && profile.base?.compatible_evaluated
    && !profile.base.compatible_printers?.includes(context.printer.baseName ?? '')) return denied('A printer-specific material takes precedence');
  return printers;
}
export function selectCompatibility(s: ProfileContextState, type: PresetType, key: string, printer = s.printer, nozzle?: string | number): Compatibility {

  const native = type !== 'machine' && hasWorksWithRule(s,type,key) ? resolvable(s,type,key,printer,nozzle).failure ?? allowed()
    : selectNativeCompatibility(s,type,key,printer,nozzle);
  if (!native.ok || type === 'machine') return native;
  const reason = worksWithReason(s,type,key,printer,nozzle);
  return reason ? denied(reason) : native;
}
