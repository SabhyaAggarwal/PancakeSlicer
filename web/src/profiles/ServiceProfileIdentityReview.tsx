import type { ProfileOverlay, UserPreset } from '../api/types';
import { Dropdown } from '../components/Dropdown';
import { Check } from '../components/Check';
import './service-profile-recovery.css';

interface Review {path:string;sha256:string;profile:UserPreset}
interface Props {
  profiles:readonly Review[];
  saved:readonly ProfileOverlay[];
  choices:Readonly<Record<string,string>>;
  busy:boolean;
  kept?:readonly {path:string;current:ProfileOverlay}[];
  onKeep?(path:string,keep:boolean):void;
  onChoose(path:string,profileId:string):void;
  onCreate(path:string):void;
}
const settings=(p:UserPreset|ProfileOverlay)=>p.overlay ?? ('overrides' in p?p.overrides:{});
const parent=(p:UserPreset|ProfileOverlay)=>p.base?.name ?? ('inherits' in p?p.inherits:'');
const value=(v:unknown)=>v===undefined?'Not set':typeof v==='string'?v:JSON.stringify(v);
const kind=(p:UserPreset|ProfileOverlay)=>p.type==='machine'?'Printer':p.type==='filament'?'Filament':'Print settings';

export function ServiceProfileIdentityReview({profiles,saved,choices,busy,kept,onKeep,onChoose,onCreate}:Props){
  return <div className="pf-service-identities">
    <p>These original files are retained. Choose an identity for each older profile before saving the recovered library. Nothing is assigned automatically.</p>
    {profiles.map(row=>{
      const p=row.profile,id=p.id ?? choices[row.path],target=saved.find(q=>q.id===id),source=settings(p),existing=target && settings(target);
      const keptRecord=kept?.find(d=>d.path===row.path)?.current;
      const differences=existing?Array.from(new Set([...Object.keys(source),...Object.keys(existing)])).filter(key=>JSON.stringify(source[key])!==JSON.stringify(existing[key])):[];
      const eligible=saved.filter(q=>q.type===p.type && !q.archived);
      return <section key={row.path} className="pf-service-identity" aria-label={`Recover ${p.name}`}>
        <h3>{p.name}</h3><p>{kind(p)} · Native parent: {parent(p) || 'Unresolved'}</p>
        {!p.base?.version && <p>Captured native release: unknown. Recovery records this explicitly; the original file has no release version.</p>}
        <p className="pf-service-file">{row.path}</p>
        {p.id?<p>Existing identity retained: <code>{p.id}</code></p>:<>
          <label>Use saved profile
            <Dropdown className="combo" aria-label={`Saved identity for ${p.name}`} value={target?.id ?? ''} disabled={busy} onValueChange={id=>onChoose(row.path,id)} options={[{value:'',label:'Choose an existing profile…'},...eligible.map(q=>({value:q.id!,label:`${q.name}${q.kind==='master'?' · Master':''}`}))]}/>
          </label>
          <button type="button" className="btn" disabled={busy || !!id && !target} onClick={()=>onCreate(row.path)}>{id && !target?'New identity confirmed':'Create a separate recovered profile'}</button>
          {!id && <p role="status">Identity required. The original file has no saved ID.</p>}
          {id && <p>Confirmed identity: <code>{id}</code></p>}
        </>}
        {target && <div>
          <p>Saved target: {target.name} · Native parent: {parent(target) || 'Unresolved'}</p>
          <p>Saved record revision: {target.revision ?? 0}{target.archived?' · Archived':''}</p>
          {p.id && onKeep && <label className="pf-service-keep"><Check checked={!!keptRecord} disabled={busy} onCheckedChange={keep=>onKeep(row.path,keep)}/><span>Keep the saved record captured at revision {keptRecord?.revision ?? target.revision ?? 0} unchanged and retain the original file for recovery.</span></label>}
          {differences.length?<><p role="status">Saved override values differ. They will not be overwritten silently; review the conflict before adoption.</p>
            <div className="pf-service-differences"><table className="pf-table"><thead><tr><th>Setting</th><th>Original file</th><th>Saved target</th></tr></thead><tbody>{differences.map(key=><tr key={key}><th scope="row">{key}</th><td>{value(source[key])}</td><td>{value(existing![key])}</td></tr>)}</tbody></table></div>
          </>:<p>Saved override values match. Native parent and metadata are also checked before adoption.</p>}
        </div>}
        <details><summary>Original record and file fingerprint</summary><p><code>{row.sha256}</code></p><pre>{JSON.stringify(p,null,2)}</pre></details>
      </section>;
    })}
  </div>;
}
