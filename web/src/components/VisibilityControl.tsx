import { savePreferences } from '../saved-preferences';
import { RadioGroup } from 'radix-ui';
import { MenuButton } from './Menu';
import { Caret } from './Caret';
import type { OptionMode } from '../options';
import { effectiveMode, setState, useStore } from '../state/store';
const choices=[{id:'simple',label:'Simple'},{id:'advanced',label:'Advanced'},{id:'develop',label:'Expert'}] as const;
export function VisibilityControl({value,onChange,disabled=false}: {value:OptionMode;onChange(value:'simple'|'advanced'|'develop'):void;disabled?:boolean}) {
  return <div className="visibility-control"><RadioGroup.Root className="seg slant-seg detail-options" aria-label="Settings detail" value={value === 'developer' ? 'develop' : value} onValueChange={v=>onChange(v as 'simple'|'advanced'|'develop')} disabled={disabled}>{choices.map(o=><RadioGroup.Item key={o.id} value={o.id} className="detail-option" data-detail={o.id}>{o.label}</RadioGroup.Item>)}</RadioGroup.Root></div>;
}

export function SettingsDetail({compact = false}: {compact?: boolean}) {
  const value=useStore(effectiveMode);
  const pick=(mode:'simple'|'advanced'|'develop')=>{
    savePreferences({'orca.settings_detail':mode,'orca.developer_mode':'0'});
    setState({uiMode:mode,developerMode:false});
  };
  if(compact){
    const current=choices.find(o=>o.id===(value === 'developer' ? 'develop' : value)) ?? choices[0];
    return <MenuButton className="btn-ghost detail-menu" label={`Settings detail: ${current.label}`} items={choices.map(o=>({label:o.label,role:'menuitemradio',checked:o.id===current.id,action:()=>pick(o.id)}))}>{current.label}<Caret/></MenuButton>;
  }
  return <VisibilityControl value={value} onChange={pick}/>;
}
