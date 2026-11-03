import { RadioGroup } from 'radix-ui';
import type { OptionMode } from '../options';
import { effectiveMode, setState, useStore } from '../state/store';
const choices=[{id:'simple',label:'Simple'},{id:'advanced',label:'Advanced'},{id:'develop',label:'Expert'}] as const;
export function VisibilityControl({value,onChange,disabled=false}: {value:OptionMode;onChange(value:'simple'|'advanced'|'develop'):void;disabled?:boolean}) {
  return <div className="visibility-control"><RadioGroup.Root className="detail-options segmented-control" aria-label="Settings detail" value={value === 'developer' ? 'develop' : value} onValueChange={v=>onChange(v as 'simple'|'advanced'|'develop')} disabled={disabled}>{choices.map(o=><RadioGroup.Item key={o.id} value={o.id} className="detail-option segmented-button" data-detail={o.id} title={disabled ? 'Wait for the profile save to finish' : `${o.label} settings`}>{o.label}</RadioGroup.Item>)}</RadioGroup.Root></div>;
}
export function SettingsDetail() {
  const value=useStore(effectiveMode);
  return <VisibilityControl value={value} onChange={mode=>{
    try {localStorage.setItem('orca.settings_detail',mode);localStorage.setItem('orca.developer_mode','0');}catch { }
    setState({uiMode:mode,developerMode:false});
  }}/>;
}
