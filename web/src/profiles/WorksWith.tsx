import { Check } from '../components/Check';
import type { Config, UserPreset } from '../api/types';
import { getState } from '../state/store';
import { listWorksWithPrinters, type ProfileApplicability } from './applicability';
import { profileSummary } from './metadata';
import { profileKey } from './identity';

export const applicabilityKey = (value?: ProfileApplicability) => !value || value.mode === 'inherit' ? 'inherit' : JSON.stringify([...new Set(value.printerIds)].sort());
export function WorksWith({source, config, value, onChange}: {source: UserPreset; config: Config; value?: ProfileApplicability; onChange(value: ProfileApplicability): void}) {
  if(source.type === 'machine') return null;
  const s = getState(), key = profileKey(source);
  const draft = {...source, applicability:value, overlay:config, overrides:config};
  const state = {...s,userPresets:s.userPresets.map(p=>profileKey(p) === key ? draft : p)};
  const rows = listWorksWithPrinters(state,source.type,key);
  const toggle = (id:string, checked:boolean) => {
    const selected = new Set(value?.mode === 'only' ? value.printerIds : rows.filter(row=>row.selected === true).map(row=>row.key));
    if(checked) selected.add(id); else selected.delete(id);
    onChange({schema:1,mode:'only',printerIds:[...selected]});
  };
  return <section className="works-with" aria-label="Works with printers">
    <div className="works-with-mode"><span>{value?.mode === 'only' ? 'Selected printers' : 'Using template compatibility'}</span><button type="button" className="btn-ghost" disabled={!value || value.mode === 'inherit'} title={!value || value.mode === 'inherit' ? 'Already using template compatibility' : 'Restore inherited compatibility in this draft'} onClick={()=>onChange({schema:1,mode:'inherit'})}>Use template compatibility</button></div>
    {rows.map(row=><label key={row.key} className="works-with-row"><Check checked={row.selected === null ? 'indeterminate' : row.selected} disabled={row.archived && row.selected !== true} title={row.archived ? 'Restore this printer before enabling it; an existing association can be unchecked' : undefined} aria-label={`Works with ${row.name}`} onCheckedChange={checked=>toggle(row.key,checked)}/><span><strong>{row.name}</strong><span className="pf-editor-summary">{profileSummary('machine',row.config)}</span>{row.archived ? <span className="works-with-reason">Removed printer — restore it or remove this association</span> : !row.native.ok && <span className="works-with-reason">{row.native.reason || 'Compatibility is unknown'}</span>}</span></label>)}
    {!rows.length && <p>Add a printer to choose where this profile can be used.</p>}
  </section>;
}
