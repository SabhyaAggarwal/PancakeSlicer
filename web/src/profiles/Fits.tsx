
import { Dropdown } from '../components/Dropdown';
import { useStore } from '../state/store';


export interface FitsRule { schema: 2; printers?: { models: string[] }; nozzle?: { sizes: number[] } }
const NOZZLES = [0.2, 0.25, 0.4, 0.6, 0.8];
const first = (v: unknown) => String(Array.isArray(v) ? v[0] ?? '' : v ?? '');

export function fitsOf(value: unknown): { models: string[]; nozzles: number[] } {
  const v = value as FitsRule | undefined;
  return { models: v?.printers?.models ?? [], nozzles: v?.nozzle?.sizes ?? [] };
}
const toRule = (models: string[], nozzles: number[]): FitsRule | undefined =>
  models.length || nozzles.length ? { schema: 2, ...(models.length ? { printers: { models } } : {}), ...(nozzles.length ? { nozzle: { sizes: nozzles } } : {}) } : undefined;

export function Fits({ value, readOnly, onChange }: { value: unknown; readOnly?: boolean; onChange(next: FitsRule | undefined): void }) {

  const models = useStore((s) => [...new Set(s.machines.map((m) => first(m.config.printer_model) || m.model || '').filter(Boolean))].sort().join('\n'));
  const { models: chosen, nozzles } = fitsOf(value);
  return <section className="fits" aria-label="Fits">
    <label className="fits-row"><span>Printer models</span>
      <Dropdown className="combo" multiple aria-label="Printer models" placeholder="Any printer" disabled={readOnly} value={chosen}
        onValueChange={(next) => onChange(toRule(next, nozzles))} options={(models ? models.split('\n') : []).map((m) => ({ value: m, label: m }))}/></label>
    <div className="fits-row" role="group" aria-label="Nozzle sizes"><span>Nozzle sizes</span>
      <div className="chips">{NOZZLES.map((n) => <button key={n} type="button" className="chip" disabled={readOnly} aria-pressed={nozzles.includes(n)}
        onClick={() => onChange(toRule(chosen, nozzles.includes(n) ? nozzles.filter((x) => x !== n) : [...nozzles, n].sort()))}>{n} mm</button>)}</div></div>
  </section>;
}
