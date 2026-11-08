import { Dropdown } from '../components/Dropdown';
import { Glyph } from '../components/Glyph';
import { useTabLoaded } from '../components/PresetDialogs';
import { OPTIONS, splitKey, TABS } from '../options';
import type { Config, ConfigValue } from '../api/types';
import { SEP } from '../separator';

const first = (v: ConfigValue | undefined) => String(Array.isArray(v) ? v[0] ?? '' : v ?? '');
const printKeys = () => [...new Set((TABS.process?.pages ?? []).flatMap((p) => p.groups.flatMap((g) => g.lines.flatMap((l) => l.options.map((k) => splitKey(k)[0])))))]
  .filter((k) => OPTIONS[k]?.label && k !== 'layer_height');
export const labelOf = (key: string) => {
  const group = TABS.process?.pages.flatMap((p) => p.groups).find((g) => g.lines.some((l) => l.options.some((k) => splitKey(k)[0] === key)))?.title;
  const own = OPTIONS[key]?.label || key;
  if (!group) return own;
  const g = group.toLowerCase(), o = own.toLowerCase();
  const words = (t: string) => new Set(t.split(/[^a-z]+/).filter((w) => w.length > 2).map((w) => w.replace(/s$/, '')));
  const shared = [...words(o)].some((w) => words(g).has(w));
  return o.includes(g) || shared && !g.includes(o) ? own : g.includes(o) ? group : `${group}${SEP}${own}`;
};

export function PrintOverrides({ value, readOnly, onChange }: { value: Config; readOnly?: boolean; onChange(next: Config): void }) {
  useTabLoaded('process');
  const keys = Object.keys(value).sort((a, b) => labelOf(a).localeCompare(labelOf(b)));
  const set = (key: string, text: string) => onChange({ ...value, [key]: Array.isArray(value[key]) ? [text] : text });
  const remove = (key: string) => { const next = { ...value }; delete next[key]; onChange(next); };
  const add = (key: string) => onChange({ ...value, [key]: (OPTIONS[key]?.default as ConfigValue | undefined) ?? '' });
  return <section className="print-overrides" aria-label="Print overrides">
    {keys.length > 0 && <table><tbody>{keys.map((k) => <tr key={k}>
      <td>{labelOf(k)}</td>
      <td><input className="text num" aria-label={labelOf(k)} disabled={readOnly} value={first(value[k])} onChange={(e) => set(k, e.target.value)}/><span className="po-unit">{OPTIONS[k]?.sidetext ?? ''}</span></td>
      <td className="po-remove">{!readOnly && <button type="button" className="reset-icon row-remove" aria-label="Remove override" onClick={() => remove(k)}><Glyph name="trash" size={14}/></button>}</td>
    </tr>)}</tbody></table>}
    {!readOnly && <Dropdown className="combo" aria-label="Add a print setting" value="" placeholder="Add a print setting" onValueChange={add}
      options={printKeys().filter((k) => !(k in value)).sort((a, b) => labelOf(a).localeCompare(labelOf(b))).map((k) => ({ value: k, label: labelOf(k) }))}/>}
  </section>;
}
