import { useId, useState } from 'react';
import { Popover } from 'radix-ui';
import { Range } from './Range';
import './controls.css';
const KEY = 'orca.web.recent-colors';
const PALETTE = ['#FFFFFF','#B6B6BE','#52525B','#17171B','#D34F56','#D99245','#E1C85B','#4C9970','#4389B5','#785EB0','#B96692','#8C6A52'];
const normalize = (s: string) => /^#?[\da-f]{6}$/i.test(s) ? `#${s.replace('#', '').toUpperCase()}` : null;
const read = (): string[] => { try { const x: unknown = JSON.parse(localStorage.getItem(KEY) ?? '[]'); return Array.isArray(x) ? x.map(String).map(normalize).filter((v): v is string => !!v).slice(0,8) : []; } catch { return []; } };
export default function ColorControl({value, onCommit, label = 'Color', disabled, ...named}: {value: string; onCommit(value: string): void; label?: string; disabled?: boolean; 'aria-labelledby'?: string}) {
  const id = useId();
  const [open, setOpen] = useState(false), [draft, setDraft] = useState(value), [recent, setRecent] = useState(read);
  const valid = normalize(draft), color = valid ?? normalize(value) ?? '#000000';
  const rgb = [1,3,5].map(i => parseInt(color.slice(i,i+2),16));
  const setChannel = (i: number, n: number) => setDraft('#' + rgb.map((v,k) => (k === i ? Math.max(0,Math.min(255,Math.round(n))) : v).toString(16).padStart(2,'0')).join('').toUpperCase());
  const apply = () => {
    if (!valid) return;
    onCommit(valid);
    const next = [valid, ...recent.filter(c => c !== valid)].slice(0,8);
    setRecent(next); try { localStorage.setItem(KEY, JSON.stringify(next)); } catch {  }
    setOpen(false);
  };
  const swatches = (colors: string[], group: string) => <div className="color-palette" role="group" aria-label={group}>{colors.map(c => <button key={c} type="button" className="color-swatch" style={{backgroundColor: c}} aria-label={c} aria-pressed={color === c} onClick={() => setDraft(c)} />)}</div>;
  return <Popover.Root open={open} onOpenChange={next => { setOpen(next); if (next) setDraft(normalize(value) ?? '#000000'); }}>
    <Popover.Trigger asChild><button {...named} type="button" className="color-trigger" style={{backgroundColor:normalize(value) ?? undefined}} data-unknown={!normalize(value)} aria-label={label} title={disabled ? `${label}: unavailable for this profile` : `${label}: ${normalize(value) ?? 'Not set'}`} disabled={disabled}>{!normalize(value) ? '?' : ''}</button></Popover.Trigger>
    <Popover.Portal><Popover.Content className="orca-menu color-popover" aria-label={label} sideOffset={6} collisionPadding={12} onKeyDown={e => e.stopPropagation()}>
      <form onSubmit={e => {e.preventDefault(); apply();}}>
        <div className="color-heading"><span>{label}</span><span className="color-preview" style={{backgroundColor: color}}/></div>
        {swatches(PALETTE, 'Palette')}
        {recent.length > 0 && <><div className="color-caption">Recent</div>{swatches(recent, 'Recent colors')}</>}
        <label className="color-hex"><span>HEX</span><input className="text" aria-label="HEX color" aria-invalid={!valid} value={draft} spellCheck={false} maxLength={7} onChange={e => setDraft(e.target.value)}/></label>
        <div className="color-channels">{['R','G','B'].map((key,i) => <div key={key} className="color-channel"><label htmlFor={`${id}-${key}`}>{key}</label><Range min={0} max={255} value={rgb[i]} onValueChange={n => setChannel(i,n)} aria-label={`${key} channel`}/><input id={`${id}-${key}`} className="text num" aria-label={`${key} value`} inputMode="numeric" type="number" min={0} max={255} value={rgb[i]} onChange={e => { if(e.target.value !== '') setChannel(i, Number(e.target.value)); }}/></div>)}</div>
        {!valid && <div className="control-error" role="alert">Enter a six-digit HEX color.</div>}
        <div className="color-actions"><Popover.Close asChild><button type="button" className="btn-ghost">Cancel</button></Popover.Close><button type="submit" className="btn" disabled={!valid} title={!valid ? 'Enter a valid HEX color first' : 'Apply color'}>Apply</button></div>
      </form>
    </Popover.Content></Popover.Portal>
  </Popover.Root>;
}
