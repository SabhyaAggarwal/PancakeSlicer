import { Check } from '../components/Check';
import type { Config, UserPreset } from '../api/types';
import { getState } from '../state/store';
import { listWorksWithPrinters, type ProfileApplicability } from './applicability';
import { modelLabel } from './metadata';
import { findCatalogBase } from './model';
import { Icon } from '../components/Icon';

const first = (v: unknown) => String(Array.isArray(v) ? v[0] ?? '' : v ?? '');
import { profileKey } from './identity';

export const applicabilityKey = (value?: ProfileApplicability) => !value || value.mode === 'inherit' ? 'inherit' : JSON.stringify([...new Set(value.printerIds)].sort());
export function WorksWith({source, config, value, onChange}: {source: UserPreset; config: Config; value?: ProfileApplicability; onChange(value: ProfileApplicability): void}) {
  if(source.type === 'machine') return null;
  const s = getState(), key = profileKey(source);
  const draft = {...source, applicability:value, overlay:config, overrides:config};
  const state = {...s,userPresets:s.userPresets.map(p=>profileKey(p) === key ? draft : p)};
  const rows = listWorksWithPrinters(state,source.type,key).filter(row=>!row.archived || row.selected === true).map(row=>{
    const printer=s.userPresets.find(p=>profileKey(p) === row.key),vendor=findCatalogBase(s,'machine',printer?.base ?? printer?.inherits ?? '').base?.vendor ?? '';
    return {...row,template:row.native.ok,model:first(row.config.printer_model) || 'Other',label:printer?.kind === 'master' ? modelLabel(row.config,vendor) : row.name,nozzle:first(row.config.nozzle_diameter)};
  });
  const overridden = (row: typeof rows[number]) => value?.mode === 'only' && row.selected !== null && row.selected !== row.template;
  const overrides = rows.filter(overridden).length;
  const set = (id: string, on: boolean) => {
    const chosen = new Set(rows.filter(row=>row.key === id ? on : row.selected === true).map(row=>row.key));
    const followsTemplate = rows.every(row=>chosen.has(row.key) === row.template);
    onChange(followsTemplate ? {schema:1,mode:'inherit'} : {schema:1,mode:'only',printerIds:[...chosen]});
  };
  const models = [...new Set(rows.map(row=>row.model))].sort();
  return <section className="works-with" aria-label="Works with printers">
    <div className="works-with-status"><span>{overrides ? `${overrides} override${overrides === 1 ? '' : 's'}` : 'Follows template'}</span>
      {value?.mode === 'only' && <button type="button" className="link" onClick={()=>onChange({schema:1,mode:'inherit'})}>Reset to template</button>}</div>
    {rows.length ? <table className="works-with-table">
      <thead><tr><th>Printer</th><th>Nozzle</th><th>Template</th><th>Works with</th></tr></thead>
      {models.map(model=><tbody key={model}>
        <tr className="works-with-group"><th colSpan={4} scope="colgroup">{model}</th></tr>
        {rows.filter(row=>row.model === model).map(row=><tr key={row.key} className={overridden(row) ? 'is-override' : undefined}>
          <th scope="row"><span className="works-with-printer"><span className="works-with-name">{row.label}</span>{row.key === s.printer && <span className="pf-used">In use</span>}</span></th>
          <td>{row.nozzle && `${row.nozzle} mm`}</td>
          <td aria-label={row.template ? 'Template includes this printer' : 'Template excludes this printer'}>{row.template ? '✓' : '—'}</td>
          <td><span className="works-with-cell"><Check checked={row.selected === null ? 'indeterminate' : row.selected} disabled={row.archived && row.selected !== true} aria-label={`Works with ${row.name}`} onCheckedChange={checked=>set(row.key,checked === true)}/>
            {overridden(row) && <button type="button" className="icon-btn" aria-label={`Reset ${row.name} to template`} onClick={()=>set(row.key,row.template)}><Icon name="undo" size={14}/></button>}</span></td>
        </tr>)}
      </tbody>)}
    </table> : <p>Add a printer to choose where this profile can be used.</p>}
  </section>;
}
