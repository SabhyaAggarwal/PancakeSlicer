import { useState } from 'react';
import { Check } from '../components/Check';
export interface FacetOption {value:string;label:string}
export function ProfileFilterGroup({label,options,value,onChange}: {label:string;options:FacetOption[];value:string[];onChange(value:string[]):void}) {
  const [expanded,setExpanded]=useState(false),[query,setQuery]=useState('');
  if(options.length < 2 && !value.length)return null;
  const matching=query ? options.filter(o=>o.label.toLocaleLowerCase().includes(query.toLocaleLowerCase())) : options;
  const shown=expanded || query ? matching : matching.slice(0,6);
  return <fieldset className="pf-facet-group"><legend>{label}</legend>
    {options.length > 12 && <input className="text" aria-label={`Find ${label.toLowerCase()}`} placeholder={`Find ${label.toLowerCase()}`} value={query} onChange={e=>setQuery(e.target.value)}/>}
    {shown.map(o=><label key={o.value} className="pf-facet-choice"><Check checked={value.includes(o.value)} onCheckedChange={checked=>onChange(checked ? [...value,o.value] : value.filter(v=>v !== o.value))}/><span>{o.label}</span></label>)}
    {!shown.length && <span className="pf-dim">No matches</span>}
    {!query && options.length > 6 && <button type="button" className="more-toggle" aria-expanded={expanded} onClick={()=>setExpanded(!expanded)}>{expanded ? 'Show less' : 'Show more'}</button>}
  </fieldset>;
}
