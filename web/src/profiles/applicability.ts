import type { PresetType } from '../api/types';
import { profileKey } from './identity';
import { resolveProfile } from './model';
import { selectNativeCompatibility } from './compatibility';
import type { ProfileContextState } from './context';
export type ProfileApplicability = { schema: 1; mode: 'inherit' } | { schema: 1; mode: 'only'; printerIds: string[] };
export function validApplicability(value: unknown): value is ProfileApplicability {
  if (!value || typeof value !== 'object') return false;
  const v = value as ProfileApplicability;
  return v.schema === 1 && (v.mode === 'inherit' || v.mode === 'only' && Array.isArray(v.printerIds) && v.printerIds.every(id => typeof id === 'string' && !!id));
}
export function resolveApplicability(s: ProfileContextState, type: PresetType, key: string, printerId = s.printer) {
  const profile = resolveProfile(s,type,key), printer = resolveProfile(s,'machine',printerId), value = profile.source?.applicability;
  const native = selectNativeCompatibility(s,type,key,printerId);
  const explicit = value?.mode === 'only', mode = explicit ? 'only' as const : 'inherit' as const;
  const invalid = value !== undefined && !validApplicability(value);
  const selected = invalid ? null : explicit ? !!printer.key && value.printerIds.includes(printer.key) : native.unknown ? null : native.ok;
  return {mode, selected, native, explicit, effective: invalid ? {ok:false,reason:'Repair the saved printer applicability'}
    : selected === false ? {ok:false,reason:explicit ? 'Not enabled for this printer' : native.reason}
    : native.ok && selected === true ? {ok:true,reason:''} : {ok:false,reason:native.reason || 'Compatibility could not be determined'}};
}
export function listWorksWithPrinters(s: ProfileContextState, type: 'filament' | 'process', key: string) {
  return s.userPresets.filter(p => p.type === 'machine').map(printer => {
    const id = profileKey(printer), resolved = resolveProfile(s,'machine',id), result = resolveApplicability(s,type,key,id);
    return {key:id,name:printer.name,archived:!!printer.archived,config:resolved.config,selected:result.selected,native:result.native,effective:result.effective};
  });
}
