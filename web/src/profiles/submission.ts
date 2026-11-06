import type { PresetType } from '../api/types';
import { profileContext, type ProfileContextState } from './context';
import { selectCompatibility } from './compatibility';
import { ProfileResolutionError } from './model';

export function validateProfileSubmission(s:ProfileContextState):void {
  const context=profileContext(s);
  const selections:[PresetType,typeof context.printer,number|undefined][]=[['machine',context.printer,undefined],['process',context.process,undefined],...context.filaments.map((p,i)=>['filament',p,i+1] as [PresetType,typeof p,number])];
  for(const [type,p,slot] of selections){
    if(p.issue)throw new ProfileResolutionError(p.issue);
    if(!p.source || !p.id || !p.engineName)throw Object.assign(new Error('Save or recover the selected profile before slicing.'),{code:'profile_unknown',recovery:'save_profile',profile:{kind:type,name:p.engineName ?? '',vendor:p.baseVendor ?? '',slot}});
    const compatibility=selectCompatibility(s,type,p.id);
    if(!compatibility.ok)throw Object.assign(new Error(`${p.name}: ${compatibility.reason}`),{code:compatibility.unknown?'profile_incompatible':'profile_not_enabled',recovery:compatibility.unknown?'repair_profile':'edit_applicability',profile:{kind:type,name:p.engineName,vendor:p.baseVendor ?? '',slot}});
  }
}
